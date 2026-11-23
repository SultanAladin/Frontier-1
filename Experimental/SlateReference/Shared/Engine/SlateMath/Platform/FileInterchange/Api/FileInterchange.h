//============================================================================================================================================
//                                                            FILEINTERCHANGE.H
//============================================================================================================================================
// 🧩 One stream surface over three file systems — paths, whole streams, and the write-verify-replace sequence.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT A PATH NAMES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the file system reports a path currently names.
/// note  🔴 Absent is one of the four rather than a refusal, because "no file is there" is an answer the
///       caller asked for and acts on. A refusal names a file system that failed to answer at all, which
///       is a different fact and is reported through `Result` instead.
/// tag   guarantee
enum class PathContent : std::uint32_t
{
    Absent    = 0u,   // [-] - nothing is at the path
    Stream    = 1u,   // [-] - a readable byte stream
    Directory = 2u,   // [-] - a directory
    Foreign   = 3u    // [-] - something else the file system holds; never opened
};

/// 🧩 What one path names, and the extent behind it.
/// tag   nonallocating, nonthrowing
struct PathReport
{
    PathContent    Content      = PathContent::Absent;   // [-]  - what is there
    std::uint64_t  SpannedBytes = 0u;                    // [B]  - meaningful only for Stream
    std::uint64_t  Revised      = 0u;                    // [ns] - host-monotonic revision stamp; zero when absent
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TRANSLATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The path and whole-stream surface, translated once over three file systems.
/// note  🔴 Nothing here interprets content. `10`'s codecs know what a stream holds and this knows only how
///       many bytes it is — a component here that recognised a format would put format knowledge in
///       `Layer0_Platform` and give every codec a second place to disagree with itself.
/// note  ⚠️ Paths cross this surface as UTF-8 and are widened inside it where the host requires wide text.
///       A path narrowed at the call site is a path that is wrong for exactly the artists whose documents
///       are not spelled in ASCII, and it is wrong invisibly until one of them opens a file.
/// tag   owning
class FileInterchange
{
public:

    /// 🧩 What a path currently names.
    /// in    Path     [-]  UTF-8
    /// out   Result  [-]  refuses with HostDenied when the file system failed to answer
    /// note  📝 An absent path is delivered as Absent rather than rejected. Refusing would make the ordinary
    ///        question "is this here" indistinguishable from a file system that failed to answer it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    static Deliver<PathReport> Resolve(const std::string& Path);

    /// 🧩 Reads a whole stream into an extent the caller then owns.
    /// in    Path     [-]  UTF-8
    /// out   Result  [-]  refuses with HostDenied when the stream cannot be opened, and with
    ///                     ExtentExhausted when it spans more than the declared ceiling
    /// note  ⚠️ Whole-stream. `StorageExchange` is the surface for a stream read by range, and a codec driven
    ///        by range arrival reads through that one rather than through this.
    /// cost  🔴
    /// tag   api, nonthrowing
    static Deliver<std::vector<std::uint8_t>> ReadStream(const std::string& Path);

    /// 🧩 Writes a whole stream, verifies what landed, and only then replaces what was there.
    /// in    Path      [-]  UTF-8; what the caller wants to end up holding the content
    /// in    Content   [-]  the bytes to write
    /// out   Result   [-]  refuses with HostDenied when the file system rejected, and with
    ///                      ExtentExhausted when what landed does not match what was written
    /// note  🔴 `48` §3's sequence, and the reason this is one routine rather than three. The content is
    ///        written beside the target, read back and compared, and only a verified stream replaces the
    ///        original. Writing over the original directly means a host that dies mid-write has destroyed
    ///        the artist's document to produce a partial one — and the moment it is most likely to die is a
    ///        long write of a large document, which is exactly the document worth keeping.
    /// note  ⚠️ The replacement is the file system's own rename, which is atomic within one file system and
    ///        is not across two. The staged stream is written **beside the target** for that reason and not
    ///        in a temporary directory that may sit on another volume.
    /// cost  🔴
    /// tag   api, nonthrowing
    static Deliver<bool> WriteStream(const std::string& Path, const std::vector<std::uint8_t>& Content);

    /// 🧩 Creates a directory and every absent directory above it.
    /// in    Path     [-]  UTF-8
    /// out   Result  [-]  refuses with HostDenied when the file system rejected
    /// note  📝 A directory that already exists is delivered rather than rejected — the caller asked for it to
    ///        be there, and it is.
    /// cost  🚩
    /// tag   api, nonthrowing
    static Deliver<bool> DeclareDirectory(const std::string& Path);

    /// 🧩 Removes what a path names, when it names a stream.
    /// out   Result  [-]  refuses with HostDenied when the file system rejected; delivers for an absent path
    /// cost  ✔️
    /// tag   api, nonthrowing
    static Deliver<bool> Reclaim(const std::string& Path);

    /// 🧩 Appends one path component to another, with exactly one separator between them.
    /// in    Leading   [-]  UTF-8; a trailing separator is neither required nor doubled
    /// in    Trailing  [-]  UTF-8
    /// out   Path      [-]  UTF-8
    /// cost  ✔️
    /// tag   api, nonthrowing
    static std::string Append(const std::string& Leading, const std::string& Trailing);

    // 📝 A read of more than this refuses rather than attempting the allocation. Every stream this surface is
    //    asked for is a document, a lowered shader or an image; a path naming something larger is a path that
    //    is wrong, and reporting that is more use than exhausting the host trying to honour it.
    static constexpr std::uint64_t StreamLimit = 4ull * 1024ull * 1024ull * 1024ull;   // [B] - largest whole read
};

}   // namespace Slate
