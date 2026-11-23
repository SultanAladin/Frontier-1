#pragma once

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"

namespace Slate
{

/// Receives the resolved theme used by SlateUI.
class ThemeReceiver
{
public:
    virtual ~ThemeReceiver() = default;
    virtual void ApplyTheme(const ThemeProfile& Profile) = 0;
};

} // namespace Slate
