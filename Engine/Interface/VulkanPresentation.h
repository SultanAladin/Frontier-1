/*==========================================================================================================================================
                                                           VULKANPRESENTATION.H
==========================================================================================================================================*/
// 🧩 Own Vulkan UI presentation while the inherited ImGui adapter renders secondary GLFW windows.

#pragma once
#include <memory>
#include <functional>
#include "Engine/Render/SurfaceRasterization.h"

struct GLFWwindow;
struct ImDrawData;

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     VULKAN UI PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

class VulkanPresentation
{
  public:
    VulkanPresentation();
    ~VulkanPresentation();
    VulkanPresentation(const VulkanPresentation &)            = delete;
    VulkanPresentation &operator=(const VulkanPresentation &) = delete;
    void                Initialize(GLFWwindow *Window, bool MaterialRendering = false);
    VulkanRasterAccess QueryRasterAccess() const;
    void ConfigureRendering(std::function<void(VkCommandBuffer, uint64_t, uint64_t)> Construct,
                            std::function<void()> Confirm);
    bool                Reconfigure(int Width, int Height);
    void                RenderMain(ImDrawData *Commands);
    void                PresentMain();
    void                AwaitCompletion() noexcept;
    void                Finalize() noexcept;

  private:
    struct PresentationAllocation;
    std::unique_ptr<PresentationAllocation> Allocation;
};
} // namespace Frontier
