/*==========================================================================================================================================
                                                         FOUNDATIONPANEL.H
==========================================================================================================================================*/
// 🧩 Lifecycle-controlled UI foundation panel; no scene rendering dependencies.

#pragma once

#include "Engine/Lifecycle/ActivationSequence.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     FOUNDATION PANEL
//------------------------------------------------------------------------------------------------------------------------

class FoundationPanel final : public ActivationSequence
{
  public:
    void ConstructInterface() noexcept;

  private:
    float InterfaceScale  = 1.0f;
    bool  ControlsEnabled = true;
    bool  ScaleDragging   = false;
};
} // namespace Frontier
