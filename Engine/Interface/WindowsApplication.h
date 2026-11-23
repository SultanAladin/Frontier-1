/*==========================================================================================================================================
                                                          WINDOWSAPPLICATION.H
==========================================================================================================================================*/
// 🧩 Share GLFW, ImGui, DPI, Vulkan presentation and severe-failure startup between native application entry points.

#pragma once
#include <functional>
#include <string>

struct GLFWwindow;

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     APPLICATION CALLBACKS
//------------------------------------------------------------------------------------------------------------------------

class VulkanPresentation;

struct ApplicationWindowCallbacks
{
    std::string Title = "Frontier";
    bool MaterialRendering = false;
    std::function<void(VulkanPresentation &)> InitializeVulkan;
    std::function<void(GLFWwindow *)> Initialize;
    std::function<void()> ConfigurePlatform;
    std::function<void()> PrepareInterface;
    std::function<void()> RecoverWindows;
    std::function<void(bool)> ConstructInterface;
    std::function<void()> AfterRender;
    std::function<void()> Finalize;
};

// 📝 Callbacks borrow the current window/context. Finalize must not throw and runs before presentation teardown.
int RunWindowsApplication(const ApplicationWindowCallbacks &Callbacks);
} // namespace Frontier
