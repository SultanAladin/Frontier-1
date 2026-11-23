//============================================================================================================================================
//                                                          CLIPBOARDEXCHANGE.H
//============================================================================================================================================
// 🧩 Text and imagery crossing to and from the operating system, taken as a copy that outlives what supplied it.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS CARRIED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One image taken from or handed to the host clipboard, unpremultiplied and eight bits per component.
/// note  🔴 Unpremultiplied, and stated rather than assumed. The host clipboard's own image carries no
///        statement about its alpha, and a premultiplied supply read as unpremultiplied darkens every partly
///        transparent texel — which reads as the imagery being wrong rather than as the convention being unsaid.
/// note  📝 Eight bits per component because that is what all three host clipboards carry. A wider source is
///        narrowed by whoever supplies it; nothing here widens a narrow one back and calls it precision.
/// tag   owning
struct ClipboardImage
{
    std::vector<std::uint8_t>  Texels = {};   // [-]  - Width × Height × 4, row order, RGBA
    std::uint32_t              Width  = 0u;   // [px] - texels per row
    std::uint32_t              Height = 0u;   // [px] - rows
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE EXCHANGE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The one place clipboard content crosses the operating-system edge.
/// note  🔴 Every read returns a **copy**. `52` §5 gates that a supplied-text source stores its text and that
///        nothing depends on a clipboard surviving — the artist copies something else a second later, and a
///        source holding a reference into the host clipboard would then describe content nobody supplied.
/// note  ⚠️ Text crosses as UTF-8 in both directions. Two of the three hosts carry wide text natively, so the
///        narrowing happens here rather than at each caller: a caller that narrowed it itself would narrow it
///        through the process code page, which silently drops every character outside it.
/// tag   owning
class ClipboardExchange
{
public:

    /// 🧩 Reads the host clipboard's text.
    /// out   Result  [-]  refuses with CapabilityAbsent when the clipboard carries no text at all, and with
    ///                     HostDenied when the host declines to open it
    /// note  📝 Carrying no text is CapabilityAbsent rather than HostDenied. An empty clipboard is ordinary
    ///        operation and `86` would otherwise report the artist's own empty clipboard as an OS failure.
    /// cost  🚩
    /// tag   api, nonthrowing
    static Deliver<std::string> ReadText();

    /// 🧩 Hands text to the host clipboard, replacing whatever it carried.
    /// in    Supplied  [-]  UTF-8; an empty supply clears the clipboard rather than refusing
    /// out   Result   [-]  refuses with HostDenied when the host declines
    /// cost  🚩
    /// tag   api, nonthrowing
    static Deliver<bool> WriteText(const std::string& Supplied);

    /// 🧩 Reads the host clipboard's imagery.
    /// out   Result  [-]  refuses with CapabilityAbsent when the clipboard carries no imagery, with
    ///                     ContentUnsupported for a layout this translation does not read, and with HostDenied
    ///                     when the host declines to open it
    /// note  🔴 The rows are delivered top-down regardless of how the host stored them. The Windows clipboard
    ///        stores its rows bottom-up under a positive height and top-down under a negative one, and a reader
    ///        that ignored the sign delivers half of all clipboard imagery vertically mirrored.
    /// cost  🔴
    /// tag   api, nonthrowing
    static Deliver<ClipboardImage> ReadImage();

    /// 🧩 Hands imagery to the host clipboard, replacing whatever it carried.
    /// in    Supplied  [-]  row order, top-down, RGBA, unpremultiplied
    /// out   Result   [-]  refuses with ContentUnsupported when the texel extent is not the stated extent, and
    ///                      with HostDenied when the host declines
    /// cost  🔴
    /// tag   api, nonthrowing
    static Deliver<bool> WriteImage(const ClipboardImage& Supplied);

    /// 🧩 Whether the host clipboard currently carries text this translation can read.
    /// out   TextCarried  [-]  false while the host declines to be asked at all
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    static bool TextCarried();

    /// 🧩 Whether the host clipboard currently carries imagery this translation can read.
    /// out   ImageCarried  [-]  false while the host declines to be asked at all
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    static bool ImageCarried();

    // 📝 🔴 A ceiling rather than an unbounded read. The host clipboard is written by any process on the machine
    //    and its extent is therefore an untrusted number; a translation that reserved whatever it was told would
    //    exhaust the process on a clipboard nobody in this application produced.
    static constexpr std::uint64_t ImageTexelLimit = 268435456ull;   // [texel] - 16384 × 16384
};

}   // namespace Slate
