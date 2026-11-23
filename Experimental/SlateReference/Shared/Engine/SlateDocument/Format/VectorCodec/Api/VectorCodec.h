//============================================================================================================================================
//                                                              VECTORCODEC.H
//============================================================================================================================================
// 🧩 `10` §1 — vector streams translated into `52`'s accepted subset, with every refusal named and positioned.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/VectorInterchange/Api/VectorInterchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT A DECODE YIELDS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One decoded vector source — the outline, and every construct the accepted subset would not take.
/// note  🔴 `52` §2: the refusals come back beside the outline rather than instead of it. A source carrying one
///        unsupported construct still decodes; refusing the whole of it would lose the artist the ninety-nine
///        paths that were fine. The whole point of positioning a refusal is that the artist can go and look.
/// tag   owning
struct DecodedOutline
{
    OutlineSpecification            Declared = {};   // [-] - the paths, in the source's own order
    std::vector<RejectedConstruct>   Rejected  = {};   // [-] - each named and positioned, per `52` §2
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Translates one vector stream into paths, refusing every construct outside `52` §2's subset by name.
/// in    Stream      [-]  the whole stream, as `StorageExchange` drained it
/// in    OriginPath  [-]  where it was read from; occupied on the file route only
/// out   Result     [-]  refuses with ContentUnsupported for a stream carrying no path at all
/// err   never throws
/// cost  🔴
/// note  🔴 `52` §2 converts strokes at **intake** and stores no width — and intake is above this line, not on
///        it. Converting one means flattening it first, and `52` §4's tolerance is resolution-relative, so a
///        codec that chose one would have fixed the resolution every later placement is resolved at. What is
///        translated here is the geometry; a stroked element is named in the refusals with its position, so the
///        artist is told rather than silently handed a filled path where they drew a line.
/// note  🔴 An open path stays open. Closing it silently changes which side of it the interior is on, and the
///        artist sees that as the fill having moved rather than as the path having been altered.
/// note  📝 The file route and the supplied-text route produce the identical specification — `52` §1 — so this
///        translation is the file route's half and nothing downstream can tell which route was taken.
/// tag   api, nonthrowing
Deliver<DecodedOutline> Translate(const std::vector<std::uint8_t>& Stream, const std::string& OriginPath);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

/// 🧩 Translates one vector source supplied as text rather than read from a file, retaining the text.
/// in    SourceText  [-]  retained on the specification, because there is no file to re-read
/// out   Result     [-]  refuses with ContentUnsupported for a source carrying no path at all
/// cost  🔴
/// note  🔴 The text is retained. A source whose only copy was a clipboard is unrecoverable after a reopen, and
///        the artist reads that as the document having lost their work — `52` §1.
/// tag   api, nonthrowing
Deliver<DecodedOutline> TranslateText(const std::string& SourceText);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
