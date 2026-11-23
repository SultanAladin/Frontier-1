//============================================================================================================================================
//                                                           TOOLSEQUENCE.CPP
//============================================================================================================================================
// 🧩 Tool declaration, the arbitration both units ask, and the capture that persists for a whole drag.

#include "SlateDocument/Document/ToolSequence/Api/ToolSequence.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE TOOLS
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> ToolIndex::Declare(const ToolSpecification& Declaring)
{
    if (Declaring.Identity.empty())
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "a tool declares no identity" });

    if (Declaring.Reserved  == PointerPrecedence::PrecedenceCount
     || Declaring.Previewed == PreviewSubject::PreviewCount
     || Declaring.Recorded  == TransactionSubject::SubjectCount)
    {
        return Deliver<std::uint32_t>::Refuse(
            { RefusalReason::ContentUnsupported, "no such precedence, preview or transaction shape" });
    }

    // 📝 A repeated identity is rejected rather than replacing the standing declaration. `Located` below resolves
    //    by identity, and two tools sharing one would make the resolution answer whichever was declared first —
    //    which is a tool the artist activates and does not get.
    for (const ToolSpecification& Held : Declared)
    {
        if (Held.Identity == Declaring.Identity)
        {
            return Deliver<std::uint32_t>::Refuse(
                { RefusalReason::ContentUnsupported, "a tool already declares that identity" });
        }
    }

    if (Declared.size() >= ToolLimit)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "the tool ceiling was reached" });

    const std::uint32_t ToolIndex = static_cast<std::uint32_t>(Declared.size());

    Declared.push_back(Declaring);

    return Deliver<std::uint32_t>::Result(ToolIndex);
}

Deliver<const ToolSpecification*> ToolIndex::Resolve(std::uint32_t ToolIndex) const
{
    if (ToolIndex >= Declared.size())
        return Deliver<const ToolSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such tool" });

    return Deliver<const ToolSpecification*>::Result(&Declared[ToolIndex]);
}

Deliver<ToolSpecification*> ToolIndex::Amend(std::uint32_t ToolIndex)
{
    if (ToolIndex >= Declared.size())
        return Deliver<ToolSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such tool" });

    return Deliver<ToolSpecification*>::Result(&Declared[ToolIndex]);
}

Deliver<std::uint32_t> ToolIndex::Located(const std::string& Identity) const
{
    for (std::size_t Index = 0u; Index < Declared.size(); ++Index)
    {
        if (Declared[Index].Identity == Identity)
            return Deliver<std::uint32_t>::Result(static_cast<std::uint32_t>(Index));
    }

    return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported, "nothing declares that tool" });
}

std::uint32_t ToolIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Declared.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

ToolIndex&        ToolSequence::Tools()         { return DeclaredTools;   }
const ToolIndex&  ToolSequence::Tools() const   { return DeclaredTools;   }
BrushIndex&       ToolSequence::Brushes()       { return DeclaredBrushes; }
const BrushIndex& ToolSequence::Brushes() const { return DeclaredBrushes; }

Deliver<bool> ToolSequence::DeclareTool(std::uint32_t ToolIndex_)
{
    if (!DeclaredTools.Resolve(ToolIndex_).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such tool" });

    SelectedToolIndex = ToolIndex_;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ToolSequence::DeclareBrush(std::uint32_t BrushIndex_)
{
    if (!DeclaredBrushes.Resolve(BrushIndex_).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such brush" });

    SelectedBrushIndex = BrushIndex_;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ToolSequence::DeclareColour(const ColourSpecification& Declaring)
{
    // 🔴 `36` §1: a colour without its space is rejected rather than assumed to be in the working space. An
    //    assumed space here is the defect `36` exists to prevent, placed where every stroke reads it.
    if (!Declaring.ColourDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the colour declares no space" });

    ActiveColour = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ToolSequence::DeclareDisplay(DisplaySubject Declaring)
{
    if (Declaring == DisplaySubject::DisplayCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such display mode" });

    CurrentDisplay = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ToolSequence::DeclareChannel(ChannelSubject Declaring)
{
    if (Declaring == ChannelSubject::ChannelCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such channel" });

    CurrentChannel = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ToolSequence::DeclareOverlay(OverlaySubject Declaring, bool PresenceEnabled)
{
    if (Declaring == OverlaySubject::OverlayCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such overlay" });

    OverlayPresent[static_cast<std::size_t>(Declaring)] = PresenceEnabled;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     POINTER ARBITRATION
//------------------------------------------------------------------------------------------------------------------------

PointerPrecedence ToolSequence::Arbitrate(bool InterfaceReported,
                                          bool ManipulatorOpen,
                                          bool StrokeOpen) const
{
    // 🔴 A standing capture answers unconditionally. `14` §4.2: capture persists for the whole drag, so a drag
    //    that began on a manipulator handle continues to address that handle after the cursor leaves the
    //    workspace, and a stroke is not stolen by a panel it passes under.
    if (CurrentCapture.CaptureDeclared)
        return CurrentCapture.Holder;

    if (InterfaceReported)
        return PointerPrecedence::Interface;

    if (ManipulatorOpen)
        return PointerPrecedence::Manipulator;

    if (StrokeOpen)
        return PointerPrecedence::Stroke;

    return PointerPrecedence::Workspace;
}

Deliver<bool> ToolSequence::OpenCapture(PointerPrecedence Precedence, const ResolvedPointer& Opened)
{
    if (Precedence == PointerPrecedence::PrecedenceCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such precedence" });

    // 🔴 A stronger claimant does **not** steal a standing capture. Arbitration happens once, before capture is
    //    taken; re-arbitrating mid-drag is the defect where a stroke stops the moment the cursor crosses a
    //    floating panel, and it is exactly the case `14` §4.2 declares against.
    if (CurrentCapture.CaptureDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "a capture already stands" });

    CurrentCapture.Holder          = Precedence;
    CurrentCapture.Opened          = Opened;
    CurrentCapture.CaptureDeclared = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> ToolSequence::ReleaseCapture()
{
    if (!CurrentCapture.CaptureDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::HostDenied, "no capture stands" });

    CurrentCapture = PointerCapture{};

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const ColourSpecification& ToolSequence::Colour() const  { return ActiveColour;    }
const PointerCapture&      ToolSequence::Capture() const { return CurrentCapture; }

Deliver<const ToolSpecification*> ToolSequence::ActiveTool() const
{
    if (SelectedToolIndex == AbsentTool)
    {
        return Deliver<const ToolSpecification*>::Refuse(
            { RefusalReason::ContentUnsupported, "no tool is active" });
    }

    return DeclaredTools.Resolve(SelectedToolIndex);
}

Deliver<const BrushSpecification*> ToolSequence::ActiveBrush() const
{
    if (SelectedBrushIndex == AbsentTool)
    {
        return Deliver<const BrushSpecification*>::Refuse(
            { RefusalReason::ContentUnsupported, "no brush is active" });
    }

    return DeclaredBrushes.Resolve(SelectedBrushIndex);
}

std::uint32_t  ToolSequence::ActiveToolIndex() const  { return SelectedToolIndex;      }
std::uint32_t  ToolSequence::ActiveBrushIndex() const { return SelectedBrushIndex;     }
DisplaySubject ToolSequence::Display() const            { return CurrentDisplay; }
ChannelSubject ToolSequence::IsolatedChannel() const    { return CurrentChannel; }

bool ToolSequence::OverlayActive(OverlaySubject Subject) const
{
    return Subject != OverlaySubject::OverlayCount
        && OverlayPresent[static_cast<std::size_t>(Subject)];
}

}   // namespace Slate
