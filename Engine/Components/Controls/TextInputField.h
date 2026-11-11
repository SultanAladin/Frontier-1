/*============================================================================================================================================
                                                         TEXTINPUTFIELD.H
============================================================================================================================================*/
// 🧩 Reusable UTF-8 text editing buffer and single-line input field control

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ControlStyle.h"

#include <algorithm>
#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct TextEditBuffer
{
    static constexpr uint32_t Capacity = 128u; // [count] - Fixed UTF-8 byte buffer capacity

    char     Text[Capacity]    {};    // [-]     - Active null-terminated UTF-8 text
    char     Restore[Capacity] {};    // [-]     - Snapshot restored on Escape cancellation
    uint32_t Length            = 0u;  // [count] - Active UTF-8 byte length
    uint32_t Caret             = 0u;  // [idx]   - Byte offset of blinking insertion caret
    uint32_t SelectionAnchor   = 0u;  // [idx]   - Byte offset of selection anchor
    float    BlinkPhase        = 0.0f;// [s]     - Elapsed blink timer phase
    float    ScrollX           = 0.0f;// [px]    - Horizontal text scroll offset
    bool     Active            = false;// [-]    - Keyboard focus active
    bool     Finalized         = false;// [-]    - Enter confirmation latched
    bool     Cancelled         = false;// [-]    - Escape cancellation latched

    [[nodiscard]] inline bool     SelectionPresent() const noexcept;
    [[nodiscard]] inline uint32_t SelectionStart() const noexcept;
    [[nodiscard]] inline uint32_t SelectionEnd() const noexcept;

    void Begin(const char* Initial) noexcept;
    void SelectAll() noexcept;
    void DeleteSelection() noexcept;
    void Insert(const char* Utf8) noexcept;
    void Backspace() noexcept;
    void DeleteForward() noexcept;
    void MoveCaret(int Delta, bool ExtendSelection) noexcept;
    void MoveHome(bool ExtendSelection) noexcept;
    void MoveEnd(bool ExtendSelection) noexcept;
    void Finalize() noexcept;
    void Cancel() noexcept;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

namespace TextInputField
{

[[nodiscard]] ControlContact Draw(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    TextEditBuffer&       Editor,
    const ControlPointer& Pointer,
    float                 FontSize = 13.0f,
    float                 Opacity  = 1.0f) noexcept;

} // namespace TextInputField

//------------------------------------------------------------------------------------------------------------------------
//                                                 INLINE IMPLEMENTATIONS
//------------------------------------------------------------------------------------------------------------------------

inline bool TextEditBuffer::SelectionPresent() const noexcept
{
    return Caret != SelectionAnchor;
}

inline uint32_t TextEditBuffer::SelectionStart() const noexcept
{
    return std::min(Caret, SelectionAnchor);
}

inline uint32_t TextEditBuffer::SelectionEnd() const noexcept
{
    return std::max(Caret, SelectionAnchor);
}

} // namespace Frontier
