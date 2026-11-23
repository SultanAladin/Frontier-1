//============================================================================================================================================
//                                                            NOTICEDIALOG.H
//============================================================================================================================================
// Theme-aware modal notices shared by settings, import/export, and future editor workflows.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/ThemeSpecification/Api/ThemeSpecification.h"

#include <cstdint>

namespace Slate
{

enum class NoticeTone : std::uint32_t
{
    Confirmation = 0u,
    Warning = 1u,
    Error = 2u
};

enum class NoticeDecision : std::uint32_t
{
    None = 0u,
    Accepted = 1u,
    Dismissed = 2u
};

class NoticeDialog
{
public:
    Deliver<bool> ConstructNoticeDialog(MotionIntegrator& Motion, RecordingSurface& Surface);
    void Advance(const PointerCondition& Pointer, double ElapsedMilliseconds);
    void Open(NoticeTone Role, const char* Title, const char* Message,
              const char* AcceptCaption = "Confirm", const char* DismissCaption = "Cancel");
    void Record(const PlaneExtent& Available, const ThemeDeclaration& Theme,
                const std::uint32_t TypographySize[8], const std::uint32_t TypographyWeight[8]);
    NoticeDecision ConsumeDecision();
    bool Opened() const { return IsOpen; }
    const PlaneExtent& Exclusion() const { return ModalExclusion; }
    void Reset();

private:
    bool Pressed(const ControlIdentity& Identity, const PlaneExtent& Extent);

    MotionIntegrator* Motion = nullptr;
    RecordingSurface* Surface = nullptr;
    ControlIndex Interaction = {};
    ControlIdentity Accept = {};
    ControlIdentity Dismiss = {};
    PointerCondition Pointer = {};
    NoticeTone Role = NoticeTone::Confirmation;
    NoticeDecision Decision = NoticeDecision::None;
    bool IsOpen = false;
    char Title[64] = {};
    char Message[256] = {};
    char AcceptCaption[32] = {};
    char DismissCaption[32] = {};
    PlaneExtent ModalExclusion = {};
};

} // namespace Slate
