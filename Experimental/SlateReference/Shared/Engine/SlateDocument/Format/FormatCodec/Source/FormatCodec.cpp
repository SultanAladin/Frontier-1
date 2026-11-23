//============================================================================================================================================
//                                                             FORMATCODEC.CPP
//============================================================================================================================================
// 🧩 Migration chain resolution over the declared version transformations.

#include "SlateDocument/Format/FormatCodec/Api/FormatCodec.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 DECLARED MIGRATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The whole migration surface, declared as data. A stream version reaches the current version only by
//    a chain assembled from these entries; there is no reader that decides anything version-dependent.
namespace
{
    constexpr DeclaredMigration MigrationOrder[] =
    {
        // 🚧 The first shipped layout is version 1, so no migration exists yet. Entries arrive here as the
        //    layout advances, and the resolution below needs no change when they do.
        { 0u, 0u }
    };

    constexpr std::uint32_t DeclaredSignature = 0x45544C53u;   // [-] - 'SLTE', least significant byte first
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ResolveMigration(const StreamHeading& Heading)
{
    if (Heading.Signature != DeclaredSignature)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "not a Slate document stream" });

    if (Heading.StreamVersion == CurrentStreamVersion)
        return Deliver<std::uint32_t>::Result(0u);

    if (Heading.StreamVersion > CurrentStreamVersion)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::VersionUnmigratable, "the stream was written by a later build" });
    }

    std::uint32_t Reached   = Heading.StreamVersion;
    std::uint32_t StepCount = 0u;
    bool          Advanced  = true;

    while (Reached < CurrentStreamVersion && Advanced)
    {
        Advanced = false;

        for (const DeclaredMigration& Declared : MigrationOrder)
        {
            if (Declared.FromVersion == Reached && Declared.ToVersion == Reached + 1u)
            {
                Reached  = Declared.ToVersion;
                Advanced = true;
                ++StepCount;
                break;
            }
        }
    }

    if (Reached != CurrentStreamVersion)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::VersionUnmigratable, "no declared migration chain reaches this build" });
    }

    return Deliver<std::uint32_t>::Result(StepCount);
}

}   // namespace Slate
