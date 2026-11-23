//============================================================================================================================================
//                                                         CLIPBOARDEXCHANGE.CPP
//============================================================================================================================================
// 🧩 The host clipboard opened, copied out of, and closed on every path — including the ones that refuse.

#include "SlateMath/Platform/ClipboardExchange/Api/ClipboardExchange.h"

// 📝 Every operating-system conditional in the repository lives under `SlateMath/Platform` — `04` §7.
#if defined(_WIN32)
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <windows.h>

    #pragma comment(lib, "user32.lib")
#endif

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE HOST EDGE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

#if defined(_WIN32)

// 📝 🔴 The host clipboard is a process-wide exclusive resource: while one process holds it open, no other can
//    read it. Every path below therefore closes it, including the refusing ones, and this holder is what makes
//    that structural rather than a rule each path is trusted to remember.
class ClipboardHolder
{
public:

    explicit ClipboardHolder(std::uint32_t AttemptLimit)
    {
        // 📝 Retried rather than rejected on the first denial. Another process holds the clipboard for the length
        //    of its own read, and the artist pressing paste while a browser is still closing it is the ordinary
        //    case — a single attempt reports a failure the artist resolves by pressing paste again.
        for (std::uint32_t Attempt = 0u; Attempt < AttemptLimit; ++Attempt)
        {
            if (OpenClipboard(nullptr) != 0)
            {
                Opened = true;
                return;
            }

            Sleep(1u);
        }
    }

    ClipboardHolder(const ClipboardHolder&)            = delete;
    ClipboardHolder& operator=(const ClipboardHolder&) = delete;

    ~ClipboardHolder()
    {
        if (Opened)
            CloseClipboard();
    }

    bool Held() const
    {
        return Opened;
    }

private:

    bool  Opened = false;   // [-] - the host granted the exclusive open
};

constexpr std::uint32_t OpenAttemptLimit = 16u;   // [-] - attempts before the host is called denied

std::string Narrow(const wchar_t* Widened)
{
    if (Widened == nullptr || Widened[0] == L'\0')
        return std::string();

    const int Narrows = WideCharToMultiByte(CP_UTF8, 0, Widened, -1, nullptr, 0, nullptr, nullptr);

    if (Narrows <= 1)
        return std::string();

    // 📝 The reported count includes the terminator, which is not carried in the string's own extent.
    std::string Narrowed(static_cast<std::size_t>(Narrows - 1), '\0');

    WideCharToMultiByte(CP_UTF8, 0, Widened, -1, Narrowed.data(), Narrows, nullptr, nullptr);

    return Narrowed;
}

std::wstring Widen(const std::string& Narrowed)
{
    if (Narrowed.empty())
        return std::wstring();

    const int Widths = MultiByteToWideChar(CP_UTF8, 0, Narrowed.c_str(), static_cast<int>(Narrowed.size()),
                                           nullptr, 0);

    if (Widths <= 0)
        return std::wstring();

    std::wstring Widened(static_cast<std::size_t>(Widths), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, Narrowed.c_str(), static_cast<int>(Narrowed.size()),
                        Widened.data(), Widths);

    return Widened;
}

#endif

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TEXT
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::string> ClipboardExchange::ReadText()
{
#if defined(_WIN32)

    if (IsClipboardFormatAvailable(CF_UNICODETEXT) == 0)
        return Deliver<std::string>::Refuse({ RefusalReason::CapabilityAbsent, "the clipboard carries no text" });

    const ClipboardHolder Holding(OpenAttemptLimit);

    if (!Holding.Held())
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the clipboard could not be opened" });

    const HANDLE Carried = GetClipboardData(CF_UNICODETEXT);

    if (Carried == nullptr)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the clipboard failed to report its text" });

    const wchar_t* Reading = static_cast<const wchar_t*>(GlobalLock(Carried));

    if (Reading == nullptr)
        return Deliver<std::string>::Refuse({ RefusalReason::HostDenied, "the clipboard extent could not be read" });

    const std::string Narrowed = Narrow(Reading);

    GlobalUnlock(Carried);

    return Deliver<std::string>::Result(Narrowed);

#else

    // 🚧 `04` §8 leaves which operating systems ship first open. The X11 and Wayland selections are a protocol
    //    exchange with the owning client rather than a read of a shared extent, so they arrive with the host
    //    rather than as a translation of this one.
    return Deliver<std::string>::Refuse({ RefusalReason::CapabilityAbsent, "no clipboard translation on this host" });

#endif
}

Deliver<bool> ClipboardExchange::WriteText(const std::string& Supplied)
{
#if defined(_WIN32)

    const ClipboardHolder Holding(OpenAttemptLimit);

    if (!Holding.Held())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard could not be opened" });

    if (EmptyClipboard() == 0)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard failed to be emptied" });

    if (Supplied.empty())
        return Deliver<bool>::Result(true);

    const std::wstring Widened = Widen(Supplied);

    // 📝 🔴 The extent is reserved through the host's own moveable allocator and **surrendered** to the clipboard
    //    on success. It is reclaimed here only on the failing paths: an extent the clipboard accepted and this
    //    then freed is one the next process to paste reads after it was released.
    const HGLOBAL Reserved = GlobalAlloc(GMEM_MOVEABLE, (Widened.size() + 1u) * sizeof(wchar_t));

    if (Reserved == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the host rejected the clipboard extent" });

    wchar_t* Writing = static_cast<wchar_t*>(GlobalLock(Reserved));

    if (Writing == nullptr)
    {
        GlobalFree(Reserved);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard extent could not be written" });
    }

    for (std::size_t Index = 0u; Index < Widened.size(); ++Index)
        Writing[Index] = Widened[Index];

    Writing[Widened.size()] = L'\0';

    GlobalUnlock(Reserved);

    if (SetClipboardData(CF_UNICODETEXT, Reserved) == nullptr)
    {
        GlobalFree(Reserved);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard rejected the text" });
    }

    return Deliver<bool>::Result(true);

#else

    (void)Supplied;

    // 🚧 `04` §8 leaves which operating systems ship first open.
    return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no clipboard translation on this host" });

#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE IMAGERY
//------------------------------------------------------------------------------------------------------------------------

Deliver<ClipboardImage> ClipboardExchange::ReadImage()
{
#if defined(_WIN32)

    if (IsClipboardFormatAvailable(CF_DIB) == 0)
        return Deliver<ClipboardImage>::Refuse({ RefusalReason::CapabilityAbsent, "the clipboard carries no imagery" });

    const ClipboardHolder Holding(OpenAttemptLimit);

    if (!Holding.Held())
        return Deliver<ClipboardImage>::Refuse({ RefusalReason::HostDenied, "the clipboard could not be opened" });

    const HANDLE Carried = GetClipboardData(CF_DIB);

    if (Carried == nullptr)
    {
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::HostDenied, "the clipboard failed to report its imagery" });
    }

    const std::uint8_t* Reading = static_cast<const std::uint8_t*>(GlobalLock(Carried));

    if (Reading == nullptr)
    {
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::HostDenied, "the clipboard extent could not be read" });
    }

    const std::size_t Spanned = static_cast<std::size_t>(GlobalSize(Carried));

    BITMAPINFOHEADER Declared = {};

    if (Spanned < sizeof(BITMAPINFOHEADER))
    {
        GlobalUnlock(Carried);
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the clipboard imagery carries no whole declaration" });
    }

    // 📝 Copied out rather than read through a cast. The clipboard extent carries no alignment guarantee for a
    //    structure read, and the declaration is small enough that copying it costs nothing measurable.
    std::uint8_t* Landing = reinterpret_cast<std::uint8_t*>(&Declared);

    for (std::size_t Index = 0u; Index < sizeof(BITMAPINFOHEADER); ++Index)
        Landing[Index] = Reading[Index];

    // 🔴 A negative height means the host stored its rows top-down; a positive one means bottom-up. `52`'s
    //    intake and `14`'s paste both read top-down, so the sign is resolved here — a reader ignoring it
    //    delivers half of all clipboard imagery vertically mirrored and blames the source application.
    const bool StoredUpward = Declared.biHeight > 0;

    const std::int64_t SignedHeight = static_cast<std::int64_t>(Declared.biHeight);
    const std::int64_t RowCount     = SignedHeight < 0 ? -SignedHeight : SignedHeight;
    const std::int64_t ColumnCount  = static_cast<std::int64_t>(Declared.biWidth);

    if (ColumnCount <= 0 || RowCount <= 0)
    {
        GlobalUnlock(Carried);
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the clipboard imagery declares no extent" });
    }

    if (static_cast<std::uint64_t>(ColumnCount) * static_cast<std::uint64_t>(RowCount) > ImageTexelLimit)
    {
        GlobalUnlock(Carried);
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::ExtentExhausted, "the clipboard imagery exceeds the declared texel ceiling" });
    }

    // 📝 Twenty-four and thirty-two bits only, and uncompressed only. The remaining layouts — palette-indexed,
    //    run-length encoded, bitfield-masked — are a decode, and `04` §4 states that nothing in `Layer0_Platform`
    //    interprets content. A caller wanting them reaches `10`'s codecs with the extent this rejected.
    if (Declared.biCompression != BI_RGB || (Declared.biBitCount != 24u && Declared.biBitCount != 32u))
    {
        GlobalUnlock(Carried);
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the clipboard imagery carries a layout this surface does not read" });
    }

    const std::size_t ComponentBytes = Declared.biBitCount == 32u ? 4u : 3u;

    // 📐 Rows are padded to a four-byte boundary, which is a property of the layout and not of the extent. A
    //    reader stepping by width times components reads every row after the first at an offset that grows by
    //    the padding — visible as an image that shears further with every row.
    const std::size_t RowStride = ((static_cast<std::size_t>(ColumnCount) * ComponentBytes + 3u) / 4u) * 4u;

    const std::size_t DeclaredBytes = Declared.biSize >= sizeof(BITMAPINFOHEADER)
                                    ? static_cast<std::size_t>(Declared.biSize)
                                    : sizeof(BITMAPINFOHEADER);

    if (Spanned < DeclaredBytes + RowStride * static_cast<std::size_t>(RowCount))
    {
        GlobalUnlock(Carried);
        return Deliver<ClipboardImage>::Refuse(
            { RefusalReason::ContentUnsupported, "the clipboard imagery is shorter than its own declaration" });
    }

    ClipboardImage Received;
    Received.Width  = static_cast<std::uint32_t>(ColumnCount);
    Received.Height = static_cast<std::uint32_t>(RowCount);
    Received.Texels.assign(static_cast<std::size_t>(ColumnCount) * static_cast<std::size_t>(RowCount) * 4u, 0u);

    const std::uint8_t* Supplied     = Reading + DeclaredBytes;
    bool                AlphaCarried = false;

    for (std::size_t Row = 0u; Row < static_cast<std::size_t>(RowCount); ++Row)
    {
        const std::size_t Sourced = StoredUpward ? static_cast<std::size_t>(RowCount) - 1u - Row : Row;

        for (std::size_t Column = 0u; Column < static_cast<std::size_t>(ColumnCount); ++Column)
        {
            const std::size_t From = Sourced * RowStride + Column * ComponentBytes;
            const std::size_t Into = (Row * static_cast<std::size_t>(ColumnCount) + Column) * 4u;

            // 📝 The host stores its components blue first. The order is the layout's, not a preference, and is
            //    reversed here so that everything above reads one order regardless of which host supplied it.
            Received.Texels[Into]      = Supplied[From + 2u];
            Received.Texels[Into + 1u] = Supplied[From + 1u];
            Received.Texels[Into + 2u] = Supplied[From];
            Received.Texels[Into + 3u] = ComponentBytes == 4u ? Supplied[From + 3u] : 255u;

            if (Received.Texels[Into + 3u] != 0u)
                AlphaCarried = true;
        }
    }

    // 🔴 An uncompressed 32-bit layout **reserves** its fourth byte rather than defining it, and most writers
    //    leave it zero. Read as alpha that is a fully transparent image, so an all-zero fourth component is
    //    taken as an unwritten one and the imagery is delivered opaque. The alternative — trusting the byte —
    //    makes every paste from the common writers land as nothing at all, which reads as the paste failing.
    // ⚠️ This is the one place where absent and zero cannot be distinguished, because the layout provides no
    //    way to say which it is. A genuinely fully transparent image is indistinguishable from an unwritten one
    //    and is delivered opaque; it is also an image carrying no visible content either way.
    if (!AlphaCarried)
    {
        for (std::size_t Index = 3u; Index < Received.Texels.size(); Index += 4u)
            Received.Texels[Index] = 255u;
    }

    GlobalUnlock(Carried);

    return Deliver<ClipboardImage>::Result(Received);

#else

    // 🚧 `04` §8 leaves which operating systems ship first open.
    return Deliver<ClipboardImage>::Refuse(
        { RefusalReason::CapabilityAbsent, "no clipboard translation on this host" });

#endif
}

Deliver<bool> ClipboardExchange::WriteImage(const ClipboardImage& Supplied)
{
#if defined(_WIN32)

    if (Supplied.Width == 0u || Supplied.Height == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the imagery declares no extent" });

    const std::size_t Wanted = static_cast<std::size_t>(Supplied.Width) * Supplied.Height * 4u;

    if (Supplied.Texels.size() != Wanted)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the texel extent is not the declared extent" });
    }

    const ClipboardHolder Holding(OpenAttemptLimit);

    if (!Holding.Held())
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard could not be opened" });

    if (EmptyClipboard() == 0)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard failed to be emptied" });

    const std::size_t RowStride    = static_cast<std::size_t>(Supplied.Width) * 4u;
    const std::size_t SuppliedSpan = sizeof(BITMAPINFOHEADER) + RowStride * Supplied.Height;

    const HGLOBAL Reserved = GlobalAlloc(GMEM_MOVEABLE, SuppliedSpan);

    if (Reserved == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted, "the host rejected the clipboard extent" });

    std::uint8_t* Writing = static_cast<std::uint8_t*>(GlobalLock(Reserved));

    if (Writing == nullptr)
    {
        GlobalFree(Reserved);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard extent could not be written" });
    }

    BITMAPINFOHEADER Declaring = {};
    Declaring.biSize           = sizeof(BITMAPINFOHEADER);
    Declaring.biWidth          = static_cast<LONG>(Supplied.Width);

    // 🔴 Negative, which declares the rows top-down. Writing a positive height and top-down rows hands every
    //    other application a mirrored image, and the mirroring is attributed to this application by everyone
    //    who pastes it.
    Declaring.biHeight      = -static_cast<LONG>(Supplied.Height);
    Declaring.biPlanes      = 1u;
    Declaring.biBitCount    = 32u;
    Declaring.biCompression = BI_RGB;
    Declaring.biSizeImage   = static_cast<DWORD>(RowStride * Supplied.Height);

    const std::uint8_t* const Declared = reinterpret_cast<const std::uint8_t*>(&Declaring);

    for (std::size_t Index = 0u; Index < sizeof(BITMAPINFOHEADER); ++Index)
        Writing[Index] = Declared[Index];

    std::uint8_t* const Landing = Writing + sizeof(BITMAPINFOHEADER);

    for (std::size_t Index = 0u; Index < static_cast<std::size_t>(Supplied.Width) * Supplied.Height; ++Index)
    {
        Landing[Index * 4u]      = Supplied.Texels[Index * 4u + 2u];
        Landing[Index * 4u + 1u] = Supplied.Texels[Index * 4u + 1u];
        Landing[Index * 4u + 2u] = Supplied.Texels[Index * 4u];
        Landing[Index * 4u + 3u] = Supplied.Texels[Index * 4u + 3u];
    }

    GlobalUnlock(Reserved);

    if (SetClipboardData(CF_DIB, Reserved) == nullptr)
    {
        GlobalFree(Reserved);
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "the clipboard rejected the imagery" });
    }

    return Deliver<bool>::Result(true);

#else

    (void)Supplied;

    // 🚧 `04` §8 leaves which operating systems ship first open.
    return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no clipboard translation on this host" });

#endif
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      WHAT IS CARRIED
//------------------------------------------------------------------------------------------------------------------------

bool ClipboardExchange::TextCarried()
{
#if defined(_WIN32)
    return IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
#else
    return false;
#endif
}

bool ClipboardExchange::ImageCarried()
{
#if defined(_WIN32)
    return IsClipboardFormatAvailable(CF_DIB) != 0;
#else
    return false;
#endif
}

}   // namespace Slate
