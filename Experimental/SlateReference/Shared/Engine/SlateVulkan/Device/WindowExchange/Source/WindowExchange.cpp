//============================================================================================================================================
//                                                            WINDOWEXCHANGE.CPP
//============================================================================================================================================
// 🧩 The surface conversion, taken through the window system that produced the handle.

#include "SlateVulkan/Device/WindowExchange/Api/WindowExchange.h"

// 📝 GLFW is included here rather than in the header. `06`'s consumers see an opaque handle and a surface,
//    and no window-system spelling reaches them — which is what lets the window system change without
//    touching anything above this file.
#include <GLFW/glfw3.h>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      CONVERSION
//------------------------------------------------------------------------------------------------------------------------

Deliver<VkSurfaceKHR> Convert(VkInstance Instance, void* NativeHandle)
{
    if (Instance == VK_NULL_HANDLE || NativeHandle == nullptr)
        return Deliver<VkSurfaceKHR>::Refuse({ RefusalReason::HostDenied, "no instance or no window" });

    VkSurfaceKHR PresentationSurface = VK_NULL_HANDLE;

    const VkResult Conversion = glfwCreateWindowSurface(Instance,
                                                        static_cast<GLFWwindow*>(NativeHandle),
                                                        nullptr,
                                                        &PresentationSurface);

    if (Conversion != VK_SUCCESS)
        return Deliver<VkSurfaceKHR>::Refuse({ RefusalReason::HostDenied, "the window system rejected a surface" });

    return Deliver<VkSurfaceKHR>::Result(PresentationSurface);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RECLAMATION
//------------------------------------------------------------------------------------------------------------------------

void Reclaim(VkInstance Instance, VkSurfaceKHR PresentationSurface)
{
    if (Instance == VK_NULL_HANDLE || PresentationSurface == VK_NULL_HANDLE)
        return;

    vkDestroySurfaceKHR(Instance, PresentationSurface, nullptr);
}

}   // namespace Slate
