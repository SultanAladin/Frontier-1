/*============================================================================================================================================
                                                              REVISIONCODEC.H
============================================================================================================================================*/
// 🧩 Binary .Baseline and .Revision section encoder & decoder for lossless parametric RevisionTracker persistence

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/SpaceContainerCodec.h"
#include "Engine/Workspaces/Revision/RevisionTracker.h"

#include <string>

namespace Frontier::Codecs
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

class RevisionEncoder
{
public:
    static void AttachRevisionSections(
        const Workspaces::RevisionTracker& Tracker,
        SpaceContainerPayload&             InOutPayload);
};

class RevisionDecoder
{
public:
    [[nodiscard]] static bool ExtractRevisionSections(
        const SpaceContainerPayload& Payload,
        Workspaces::RevisionTracker& OutTracker,
        std::string&                 OutDiagnostic);
};

struct RevisionCodec
{
    using Encoder = RevisionEncoder;
    using Decoder = RevisionDecoder;
};

} // namespace Frontier::Codecs
