//============================================================================================================================================
//                                                               IMAGECODEC.H
//============================================================================================================================================
// 🧩 `10` §1 — image streams translated to the texels the file carried, at the depth it carried them, and nothing else.

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

/// 🧩 Which stream layout the leading bytes identify. Named so a refusal says what was found, not merely that it failed.
/// note  ⚠️ `10` §5 leaves which formats ship in the first codec open, and this enumeration is where that answer
///        lives. Adding one is an entry here and a branch in the translation; nothing above this line changes,
///        because every layout produces the same `DecodedImage`.
/// tag   guarantee
enum class ImageContentSubject : std::uint32_t
{
    Unrecognised      = 0u,   // [-] - the leading bytes match no layout below
    PortableNetwork   = 1u,   // [-] - PNG, by its eight-byte signature
    JointPhotographic = 2u,   // [-] - JPEG, by its start-of-image marker
    Radiance          = 3u,   // [-] - Radiance HDR, floating point and unbounded above
    Truevision        = 4u,   // [-] - TGA, which carries no signature and is identified last
    ContentCount      = 5u    // [-] - the closed count, never a layout
};

/// 🧩 How many leading bytes `ClassifyContent` reads. A codec declares this range first and the rest after.
/// note  📝 `10` §1: a decode is driven by range arrival. Classifying from a short leading range is what lets a
///        codec refuse a stream it cannot read without having waited for the whole of it to land.
inline constexpr std::uint32_t SignatureExtent = 32u;   // [B] - enough for every signature named above

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Identifies a stream's layout from its leading bytes alone, without decoding any of it.
/// in    Leading  [-]  the first bytes of the stream; fewer than SignatureExtent is accepted and classifies less
/// out   Subject  [-]  Unrecognised where no layout matches, which is a refusal the caller reports
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ImageContentSubject ClassifyContent(const std::vector<std::uint8_t>& Leading);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact,
                         PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Translates one image stream into the texels it contained, retaining its component depth verbatim.
/// in    Stream      [-]  the whole stream, as `StorageExchange` drained it
/// in    OriginPath  [-]  where it was read from; carried into the record, never parsed for meaning
/// out   Result     [-]  refuses with ContentUnsupported for an unrecognised layout or a stream the decoder
///                        rejected, and with ExtentExhausted for a stream too long to address
/// err   never throws; the decoder's own reason is carried verbatim in the refusal
/// cost  🔴
/// pre   the stream has landed in full — a partial stream classifies but does not translate
/// note  🔴 `10` §1: no colour space is declared here, because none of the layouts above carries one that this
///        decoder exposes. Declaring one anyway is the defect the gate exists to prevent — an image with an
///        assumed space is a colour error with no traceable origin. `50` §3 records the assumption at intake
///        instead, where it is reported through `86` rather than made silently.
/// note  🔴 Bit depth is retained, never narrowed. A sixteen-bit source arrives as sixteen-bit texels and a
///        Radiance stream arrives as single-precision, because narrowing here is a precision loss nobody can
///        afterwards attribute to anything.
/// tag   api, nonthrowing
Deliver<DecodedImage> Translate(const std::vector<std::uint8_t>& Stream, const std::string& OriginPath);

SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
