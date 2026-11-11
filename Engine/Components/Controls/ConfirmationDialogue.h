/*============================================================================================================================================
                                                      CONFIRMATIONDIALOGUE.H
============================================================================================================================================*/
// 🧩 Reusable foreground modal confirmation dialogue supporting Rounded (28px) and SemanticHeader (8px) layouts

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlStyle.h"

#include <cstdint>
#include <string>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DialogueSeverity : uint8_t
{
    Notice  = 0, // [-] - Neutral confirmation prompt
    Success = 1, // [-] - Positive confirmation prompt
    Warning = 2, // [-] - Discard or caution prompt
    Error   = 3, // [-] - Destructive or error prompt
};

enum class DialogueLayoutStyle : uint8_t
{
    Rounded        = 0, // [-] - 28 px rounded modal surface with tinted circular icon emblem
    SemanticHeader = 1, // [-] - 8 px modal surface with colored top banner and 120 px watermark icon
};

enum class DialogueResponse : uint8_t
{
    None      = 0, // [-] - Modal remains open awaiting user choice
    Confirmed = 1, // [-] - Primary button or Enter activated
    Cancelled = 2, // [-] - Cancel button, close cross, or Escape activated
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ConfirmationSpecification
{
    DialogueSeverity    Severity     = DialogueSeverity::Notice;        // [-] - Semantic tone selection
    DialogueLayoutStyle Layout       = DialogueLayoutStyle::Rounded;    // [-] - Visual surface geometry preset
    std::string         Title        = "Apply changes?";                // [-] - Modal heading text
    std::string         Description  = "";                              // [-] - Wrapped explanation text
    std::string         PrimaryLabel = "Apply";                         // [-] - Primary confirmation button caption
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ConfirmationDialogue
{
public:
    ConfirmationDialogue() noexcept = default;

    bool Activate(const ConfirmationSpecification& Specification);
    void Reset() noexcept;

    [[nodiscard]] bool        QueryOpen() const noexcept;
    [[nodiscard]] Rectangle2D QueryBounds() const noexcept;
    [[nodiscard]] Rectangle2D QueryPrimaryBounds() const noexcept;
    [[nodiscard]] Rectangle2D QueryCancelBounds() const noexcept;

    [[nodiscard]] DialogueResponse DrawModal(
        PixelSpace&           Surface,
        Rectangle2D           ViewportLogicalBounds,
        const ControlPointer& Pointer);

private:
    ConfirmationSpecification ActiveSpec    {};    // [-] - Currently displayed modal specification
    bool                      Active        = false; // [-] - True while modal is open
    Rectangle2D               ModalBounds   {};    // [px] - Last drawn modal surface bounds
    Rectangle2D               PrimaryBounds {};    // [px] - Last drawn primary button bounds
    Rectangle2D               CancelBounds  {};    // [px] - Last drawn cancel button bounds
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool ConfirmationDialogue::QueryOpen() const noexcept
{
    return Active;
}

inline Rectangle2D ConfirmationDialogue::QueryBounds() const noexcept
{
    return ModalBounds;
}

inline Rectangle2D ConfirmationDialogue::QueryPrimaryBounds() const noexcept
{
    return PrimaryBounds;
}

inline Rectangle2D ConfirmationDialogue::QueryCancelBounds() const noexcept
{
    return CancelBounds;
}

} // namespace Frontier
