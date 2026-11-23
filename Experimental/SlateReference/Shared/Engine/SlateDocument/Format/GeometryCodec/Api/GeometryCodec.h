//============================================================================================================================================
//                                                             GEOMETRYCODEC.H
//============================================================================================================================================
// 🧩 `10` §1 — polygon streams translated exactly as the file wrote them, n-gons and degeneracies included.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/AssetInterchange/Api/AssetInterchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE ACCEPTED SUBSET
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which polygon stream layout is being translated.
/// note  ⚠️ Wavefront carries no signature, so it is identified from the origin's suffix and not from its
///        content — the one classification in the engine that reads a name. A stream whose suffix says one
///        thing and whose content says another is rejected by the translation, not by the classification.
/// note  🚧 `10` §5 leaves the shipped set open. A second layout is an entry here and a branch in the
///        translation; `DecodedTopology` does not change, because it is the handover shape for all of them.
/// tag   guarantee
enum class GeometryContentSubject : std::uint32_t
{
    Unrecognised = 0u,   // [-] - the origin names no layout below
    Wavefront    = 1u,   // [-] - OBJ — corner runs of any count, in the file's own winding
    ContentCount = 2u    // [-] - the closed count, never a layout
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Identifies a polygon stream's layout from the suffix of the origin it was read from.
/// in    OriginPath  [-]  the origin; compared case-insensitively against the suffixes above
/// out   Subject     [-]  Unrecognised where the suffix names no layout
/// cost  ✔️
/// tag   api, nonthrowing
GeometryContentSubject ClassifyContent(const std::string& OriginPath);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact,
                         PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Translates one polygon stream into the topology it contained, repaired in no respect.
/// in    Stream      [-]  the whole stream, as `StorageExchange` drained it
/// in    OriginPath  [-]  where it was read from; carried into the record and used to classify the layout
/// out   Result     [-]  refuses with ContentUnsupported for an unrecognised layout or a stream the parser
///                        rejected, and with ExtentExhausted for a stream carrying no face at all
/// err   never throws; the vendored parser's allocation is released on every path out
/// cost  🔴
/// note  🔴 `50` §2 ① and `38`'s non-mutation rule: nothing is welded, rewound, triangulated or dropped. A run
///        of fewer than three corners is handed over as it arrived and rejected by `TopologyStructure::DeclareFace`,
///        which is where that refusal is declared to happen. A codec that dropped it would have produced a
///        specification that no longer describes the file the artist supplied.
/// note  🔴 Perpendiculars survive only where the stream indexes them exactly as it indexes positions. Where a
///        corner carries a perpendicular its vertex does not, per-vertex storage can hold only one of the two —
///        so none is emitted and `per-corner surface perpendiculars` is named in `UnsupportedNamed`, where `86`
///        reports it. Averaging them would be a repair, and picking one would be a silent loss.
/// note  📝 Wavefront declares no unit convention, so `UnitScaleDeclared` is false and `50` §3 records the
///        assumption at intake. Materials referenced through an external library are not read — the codec
///        translates the stream it was handed — but each face still carries the enrollment its directives set.
/// tag   api, nonthrowing
Deliver<DecodedTopology> Translate(const std::vector<std::uint8_t>& Stream, const std::string& OriginPath);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
