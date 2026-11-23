/*==========================================================================================================================================
                                                        CONFIRMATIONDIALOGUE.H
==========================================================================================================================================*/
// 🧩 Reusable semantic confirmation popup with native modal focus and shared control visuals.

#pragma once

#include "NotificationSequence.h"
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONFIRMATION PROFILE
//------------------------------------------------------------------------------------------------------------------------

enum class ConfirmationPresentationCategory
{
    Rounded,
    SemanticHeader
};

enum class ConfirmationResponseCategory
{
    None,
    Cancelled,
    Confirmed
};

struct ConfirmationSpecification
{
    std::string                      Title;
    std::string                      Description;
    std::string                      PrimaryLabel = "Apply";
    NotificationCategory             Category     = NotificationCategory::Information;
    ConfirmationPresentationCategory Presentation = ConfirmationPresentationCategory::Rounded;
};

class ConfirmationDialogue
{
  public:
    bool                                       Activate(const ConfirmationSpecification &Specification);
    [[nodiscard]] ConfirmationResponseCategory Construct(float InterfaceScale);
    void                                       Reset() noexcept;
    [[nodiscard]] bool                         QueryOpen() const noexcept
    {
        return Open;
    }
    [[nodiscard]] PlaneExtent QueryPrimaryExtent() const noexcept
    {
        return PrimaryExtent;
    }
    [[nodiscard]] PlaneExtent QueryCancelExtent() const noexcept
    {
        return CancelExtent;
    }
    [[nodiscard]] PlaneExtent QueryExtent() const noexcept
    {
        return Extent;
    }

  private:
    ConfirmationSpecification Profile;
    bool                      Open            = false;
    bool                      Opening         = false;
    bool                      KeyboardPrimary = false;
    PlaneExtent               Extent, PrimaryExtent, CancelExtent;
};

} // namespace Frontier
