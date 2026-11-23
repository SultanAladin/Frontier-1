//============================================================================================================================================
//                                                       THEMEINTERCHANGE.H
//============================================================================================================================================
// 🧩 The appearance file — one archive transcribed from a text stream, and inscribed back to it when the artist changes a colour.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        EXTENTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The file is a fixed shape — six appearances and eight accents — so its extent is known rather than
//    discovered. The ceiling is generous against that shape: the inscribed form runs near four kilobytes,
//    and anything an order of magnitude larger is not this file and is rejected rather than parsed.
inline constexpr std::uint32_t ArchiveLimit = 65536u;   // [B] - largest appearance stream accepted
inline constexpr std::uint32_t PathLimit    = 512u;     // [-] - longest resolved path, NUL included

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Reads and writes the appearance file. Knows the text form; knows nothing about how a panel draws it.
/// note  🔴 Hand-written rather than reached through a vendored parser, and deliberately. `SlateUI` declares
///        no unit requirement — the reach that would let it call `FileInterchange` or a package-root header
///        is exactly the permission the partition withholds, and `VerifyPartition` checks it rather than
///        trusting it. The file is a flat table of named colours; the twenty lines below read it, and that
///        is a smaller cost than the reach.
/// note  ⚠️ The form is TOML-shaped — sections, `key = value`, `#` comments — and is not general TOML. Arrays,
///        inline tables and multi-line strings are not read. A file using them is rejected, not half-read.
/// tag   api, nonallocating, nonthrowing
class ThemeInterchange
{
public:

    /// 🧩 Reads one appearance file and produces the archive it declares.
    /// in    Path      [-]  NUL-terminated, UTF-8
    /// out   ThemeArchive  [-]  every appearance the file did not mention keeps its compiled-in declaration
    /// err   HostDenied          the path could not be opened, or was not read whole
    /// err   ContentUnsupported  the stream exceeded ArchiveLimit, or a line was not understood
    /// note  A file that names only `[selection]` is valid and common: it records which appearance the artist
    ///       chose while leaving what that appearance contains to the build.
    /// cost  🔴
    static Deliver<ThemeArchive> Transcribe(const char* Path);

    /// 🧩 Writes one archive as the appearance file, replacing whatever was there.
    /// in    Path      [-]  NUL-terminated, UTF-8
    /// in    Recorded  [-]  written whole, so the file always round-trips through Transcribe
    /// err   HostDenied  the staged stream could not be opened, written whole, or moved into place
    /// note  🔴 Staged beside the destination and moved over it once complete, never written in place. A host
    ///        that stops mid-write would otherwise leave a half-file that the next run refuses, and the
    ///        artist's appearance would be lost to a crash that had nothing to do with it.
    /// cost  🔴
    static Deliver<bool> Inscribe(const char* Path, const ThemeArchive& Recorded);

    /// 🧩 Resolves a leaf name against the folder the running executable sits in.
    /// in    ExecutablePath  [-]  argv[0] as the host received it
    /// in    Leaf            [-]  the file name to place beside it
    /// out   Produced        [-]  NUL-terminated; written only when delivered
    /// in    Limit         [-]  extent of Produced, NUL included
    /// err   ExtentExhausted  the assembled path would not fit within Limit
    /// note  An executable path carrying no separator resolves to the bare leaf, which reads the working
    ///       directory — the right answer when a host is launched from the folder it lives in.
    /// cost  ✔️
    static Deliver<bool> Beside(const char*   ExecutablePath,
                                const char*   Leaf,
                                char*         Produced,
                                std::uint32_t Limit);

    /// 🧩 The leaf name every host reads its appearance from.
    /// cost  ✔️
    static const char* CurrentLeaf();

    /// 🧩 Reads the appearance sitting beside the executable and adopts it, so every panel draws from it.
    /// in    ExecutablePath  [-]  argv[0] as the host received it
    /// out   Produced        [-]  the selection the file recorded; left untouched when nothing is adopted
    /// err   HostDenied          no appearance file sits beside the executable — the ordinary first run
    /// err   ContentUnsupported  a file is there and could not be read; the build's appearance is kept
    /// note  🔴 The standing appearance is left whole on refusal rather than half-replaced. A host that
    ///        cannot read its appearance file should present the appearance it was built with, which is a
    ///        window an artist recognises — not a window drawn from a partially adopted archive.
    /// use   Called once at startup, before the first panel is recorded.
    /// cost  🔴
    static Deliver<bool> AdoptBeside(const char* ExecutablePath, ThemeSelection& Produced);

    /// 🧩 Writes the standing appearance and the given selection to the file beside the executable.
    /// in    ExecutablePath  [-]  argv[0] as the host received it
    /// in    Selected        [-]  what the Control Centre currently has chosen
    /// err   HostDenied       the file could not be written or moved into place
    /// err   ExtentExhausted  the resolved path exceeds PathLimit
    /// use   Called when the artist changes a colour, not every tick.
    /// cost  🔴
    static Deliver<bool> RecordBeside(const char* ExecutablePath, const ThemeSelection& Selected);
};

}   // namespace Slate
