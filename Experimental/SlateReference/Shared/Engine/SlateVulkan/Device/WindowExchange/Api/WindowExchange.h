//============================================================================================================================================
//                                                             WINDOWEXCHANGE.H
//============================================================================================================================================
// 🧩 Native window handle ⇄ VkSurfaceKHR — the one place the window system meets the vendor edge.

#pragma once

#include "Foundation/DeliveryGuarantee.h"

#include <vulkan/vulkan.h>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CONVERSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Converts a native window handle from `04` into a presentation surface.
/// in    Instance     [-]  the instance the surface is created against
/// in    NativeHandle [-]  the opaque handle `WindowInterchange` surrendered; never a GLFW spelling above
/// out   Result      [-]  refuses with HostDenied when the window system declines the surface
/// note  ⚠️ `WindowInterchange` in `SlateMath` and `WindowExchange` here are distinct and both required.
///       The split is what keeps `SlateMath` free of a Vulkan header.
/// cost  🚩
/// tag   api, nonthrowing
Deliver<VkSurfaceKHR> Convert(VkInstance Instance, void* NativeHandle);

/// 🧩 Destroys a surface previously converted.
/// cost  ✔️
/// tag   api, nonthrowing
void Reclaim(VkInstance Instance, VkSurfaceKHR PresentationSurface);

}   // namespace Slate
