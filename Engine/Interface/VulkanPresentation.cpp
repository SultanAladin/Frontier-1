/*==========================================================================================================================================
                                                          VULKANPRESENTATION.CPP
==========================================================================================================================================*/
// 🧩 Adapt pinned ImGui Vulkan presentation to the shared editor without importing a scene renderer or Vulkan SDK loader.

#include "VulkanPresentation.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
// 📝 GLFW_INCLUDE_NONE suppresses its default GL/gl.h include (not every platform ships OpenGL
// dev headers, and this file never uses OpenGL); GLFW_INCLUDE_VULKAN still pulls in vulkan.h.
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace Frontier
{
namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     RESULT CONSTRAINTS
//------------------------------------------------------------------------------------------------------------------------

thread_local bool ReclamationEnabled = false;

// 📝 Name every failure code in diagnostics: a bare integer like -1000069000
// (VK_ERROR_OUT_OF_POOL_MEMORY) cost a full investigation to decode from a user screenshot.
const char *DescribeResult(VkResult Result)
{
    switch (Result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    default: return "VK_RESULT_UNKNOWN";
    }
}
void RequireSuccess(VkResult Result)
{
    if (Result < 0)
    {
        DiagnosticJournal::Append("VulkanError", "VkResult=" + std::to_string(static_cast<int>(Result)) +
            " (" + DescribeResult(Result) + ")" + (ReclamationEnabled ? " during reclamation" : ""));
        if (!ReclamationEnabled)
            throw std::runtime_error("Vulkan UI failure: " + std::to_string(static_cast<int>(Result)) +
                " (" + DescribeResult(Result) + ")");
    }
}

template <typename Function> Function Resolve(VkInstance Instance, const char *Name)
{
    auto Address = glfwGetInstanceProcAddress(Instance, Name);
    if (!Address)
        throw std::runtime_error(std::string("Vulkan entry point unavailable: ") + Name);
    return reinterpret_cast<Function>(Address);
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRESENTATION ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

struct VulkanPresentation::PresentationAllocation
{
    VkInstance               Instance       = VK_NULL_HANDLE;
    VkPhysicalDevice         PhysicalDevice = VK_NULL_HANDLE;
    VkDevice                 Device         = VK_NULL_HANDLE;
    VkQueue                  Queue          = VK_NULL_HANDLE;
    uint32_t                 QueueFamily    = 0;
    VkQueueFlags             QueueCapabilities = 0;
    ImGui_ImplVulkanH_Window Main;
    bool                     RendererReady = false;
    bool                     Rebuild       = false;
    bool                     Submitted     = false;
    uint64_t SubmissionSerial = 0, CompletedSerial = 0;
    std::vector<uint64_t> ImageSerials;
    std::function<void(VkCommandBuffer, uint64_t, uint64_t)> ConstructGpu;
    std::function<void()> ConfirmGpu;

#define FRONTIER_VULKAN_FUNCTIONS(Function)                                                                            \
    Function(GetPhysicalDeviceProperties) Function(DestroyInstance) Function(EnumeratePhysicalDevices)                 \
        Function(GetPhysicalDeviceQueueFamilyProperties) Function(GetPhysicalDeviceSurfaceSupportKHR)                  \
            Function(CreateDevice) Function(GetDeviceQueue) Function(DestroyDevice) Function(DeviceWaitIdle)           \
                Function(DestroySurfaceKHR) Function(AcquireNextImageKHR) Function(WaitForFences)                      \
                    Function(ResetFences) Function(ResetCommandPool) Function(BeginCommandBuffer)                      \
                        Function(CmdBeginRenderPass) Function(CmdEndRenderPass) Function(EndCommandBuffer)             \
                            Function(QueueSubmit) Function(QueuePresentKHR)                                            \
                                Function(EnumerateDeviceExtensionProperties)
#define FRONTIER_DECLARE_FUNCTION(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_VULKAN_FUNCTIONS(FRONTIER_DECLARE_FUNCTION)
#undef FRONTIER_DECLARE_FUNCTION

    void Initialize(GLFWwindow *Window, bool MaterialRendering)
    {
        if (!glfwVulkanSupported())
            throw std::runtime_error("Vulkan loader/driver unavailable.");
        const auto CreateInstance = Resolve<PFN_vkCreateInstance>(VK_NULL_HANDLE, "vkCreateInstance");
        uint32_t   ExtensionCount = 0;
        const auto Extensions     = glfwGetRequiredInstanceExtensions(&ExtensionCount);
        if (!Extensions || !ExtensionCount)
            throw std::runtime_error("GLFW Vulkan surface extensions unavailable.");
        VkApplicationInfo Application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        Application.pApplicationName = "Frontier Editor";
        Application.apiVersion       = MaterialRendering ? VK_API_VERSION_1_2 : VK_API_VERSION_1_1;
        VkInstanceCreateInfo Description{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        Description.pApplicationInfo        = &Application;
        Description.enabledExtensionCount   = ExtensionCount;
        Description.ppEnabledExtensionNames = Extensions;
        RequireSuccess(CreateInstance(&Description, nullptr, &Instance));
#define FRONTIER_RESOLVE_FUNCTION(Name) Name = Resolve<PFN_vk##Name>(Instance, "vk" #Name);
        FRONTIER_VULKAN_FUNCTIONS(FRONTIER_RESOLVE_FUNCTION)
#undef FRONTIER_RESOLVE_FUNCTION
        if (!ImGui_ImplVulkan_LoadFunctions(
                VK_API_VERSION_1_1,
                [](const char *Name, void *Context) -> PFN_vkVoidFunction
                {
                    return reinterpret_cast<PFN_vkVoidFunction>(
                        glfwGetInstanceProcAddress(*static_cast<VkInstance *>(Context), Name));
                },
                &Instance))
            throw std::runtime_error("ImGui Vulkan entry point loading failed.");
        RequireSuccess(glfwCreateWindowSurface(Instance, Window, nullptr, &Main.Surface));
        uint32_t DeviceCount = 0;
        RequireSuccess(EnumeratePhysicalDevices(Instance, &DeviceCount, nullptr));
        std::vector<VkPhysicalDevice> Devices(DeviceCount);
        RequireSuccess(EnumeratePhysicalDevices(Instance, &DeviceCount, Devices.data()));
        for (auto Candidate : Devices)
        {
            VkPhysicalDeviceProperties DeviceProperties{};
            GetPhysicalDeviceProperties(Candidate, &DeviceProperties);
            DiagnosticJournal::Append("VulkanDevice", std::string(DeviceProperties.deviceName) +
                " vendor=" + std::to_string(DeviceProperties.vendorID) + " device=" + std::to_string(DeviceProperties.deviceID) +
                " api=" + std::to_string(DeviceProperties.apiVersion) + " driver=" + std::to_string(DeviceProperties.driverVersion));
            if (DeviceProperties.apiVersion < Application.apiVersion)
                continue;
            uint32_t PropertyCount = 0;
            RequireSuccess(EnumerateDeviceExtensionProperties(Candidate, nullptr, &PropertyCount, nullptr));
            std::vector<VkExtensionProperties> Properties(PropertyCount);
            RequireSuccess(EnumerateDeviceExtensionProperties(Candidate, nullptr, &PropertyCount, Properties.data()));
            bool SwapchainSupported = false;
            for (const auto &Property : Properties)
                SwapchainSupported |= std::string(Property.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
            if (!SwapchainSupported)
                continue;
            uint32_t QueueCount = 0;
            GetPhysicalDeviceQueueFamilyProperties(Candidate, &QueueCount, nullptr);
            std::vector<VkQueueFamilyProperties> Queues(QueueCount);
            GetPhysicalDeviceQueueFamilyProperties(Candidate, &QueueCount, Queues.data());
            for (uint32_t Index = 0; Index < QueueCount; ++Index)
            {
                VkBool32 PresentationSupported = VK_FALSE;
                RequireSuccess(
                    GetPhysicalDeviceSurfaceSupportKHR(Candidate, Index, Main.Surface, &PresentationSupported));
                if ((Queues[Index].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                    (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT) && PresentationSupported)
                {
                    PhysicalDevice = Candidate;
                    QueueFamily = Index;
                    QueueCapabilities = Queues[Index].queueFlags;
                    DiagnosticJournal::Append("VulkanDevice", "Selected " + std::string(DeviceProperties.deviceName) + " graphics queue=" + std::to_string(Index));
                    break;
                }
            }
            if (PhysicalDevice)
                break;
        }
        if (!PhysicalDevice)
            throw std::runtime_error("No Vulkan graphics queue supports this GLFW window surface.");
        constexpr float         Priority = 1;
        VkDeviceQueueCreateInfo QueueDescription{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        QueueDescription.queueFamilyIndex     = QueueFamily;
        QueueDescription.queueCount           = 1;
        QueueDescription.pQueuePriorities     = &Priority;
        const char        *DeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo DeviceDescription{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        DeviceDescription.queueCreateInfoCount    = 1;
        DeviceDescription.pQueueCreateInfos       = &QueueDescription;
        DeviceDescription.enabledExtensionCount   = 1;
        DeviceDescription.ppEnabledExtensionNames = DeviceExtensions;
        RequireSuccess(CreateDevice(PhysicalDevice, &DeviceDescription, nullptr, &Device));
        GetDeviceQueue(Device, QueueFamily, 0, &Queue);
        constexpr VkFormat Formats[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};
        Main.SurfaceFormat           = ImGui_ImplVulkanH_SelectSurfaceFormat(PhysicalDevice, Main.Surface, Formats, 2,
                                                                             VK_COLORSPACE_SRGB_NONLINEAR_KHR);
        constexpr VkPresentModeKHR Modes[] = {VK_PRESENT_MODE_FIFO_KHR};
        Main.PresentMode = ImGui_ImplVulkanH_SelectPresentMode(PhysicalDevice, Main.Surface, Modes, 1);
        int Width, Height;
        glfwGetFramebufferSize(Window, &Width, &Height);
        if (Width <= 0 || Height <= 0)
            throw std::runtime_error("Cannot initialize Vulkan presentation with a zero-size window.");
        ImGui_ImplVulkanH_CreateOrResizeWindow(Instance, PhysicalDevice, Device, &Main, QueueFamily, nullptr, Width,
                                               Height, 2, 0);
        ImGui_ImplVulkan_InitInfo Initialization{};
        Initialization.ApiVersion                   = Application.apiVersion;
        Initialization.Instance                     = Instance;
        Initialization.PhysicalDevice               = PhysicalDevice;
        Initialization.Device                       = Device;
        Initialization.QueueFamily                  = QueueFamily;
        Initialization.Queue                        = Queue;
        // 📝 Sized and accounted, not guessed: hosts bind a bounded set of user textures (material atlas,
        // preview + frame-lagged retirements, pattern atlas, ≤9 coverage slots, icon atlas + 1 retired)
        // and reclaim them; 512 leaves generous headroom while still failing loudly on a real leak.
        Initialization.DescriptorPoolSize           = 512;
        Initialization.MinImageCount                = 2;
        Initialization.ImageCount                   = Main.ImageCount;
        Initialization.PipelineInfoMain.RenderPass  = Main.RenderPass;
        Initialization.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        Initialization.CheckVkResultFn              = RequireSuccess;
        RendererReady                               = ImGui_ImplVulkan_Init(&Initialization);
        if (!RendererReady)
            throw std::runtime_error("ImGui Vulkan initialization failed.");
        Main.ClearValue.color = {{.065f, .065f, .07f, 1}};
    }

    void Finalize() noexcept
    {
        const bool PreviousReclamation = ReclamationEnabled;
        ReclamationEnabled             = true;
        if (Device && DeviceWaitIdle)
            (void)DeviceWaitIdle(Device);
        // 📝 Destroy secondary swapchains before the shared Vulkan device and before GLFW window reclamation.
        if (RendererReady)
        {
            ImGui::DestroyPlatformWindows();
            ImGui_ImplVulkan_Shutdown();
            RendererReady = false;
        }
        if (Device && Main.Swapchain)
            ImGui_ImplVulkanH_DestroyWindow(Instance, Device, &Main, nullptr);
        if (Main.Surface && DestroySurfaceKHR)
            DestroySurfaceKHR(Instance, Main.Surface, nullptr);
        Main.Surface = VK_NULL_HANDLE;
        if (Device && DestroyDevice)
            DestroyDevice(Device, nullptr);
        Device = VK_NULL_HANDLE;
        if (Instance && DestroyInstance)
            DestroyInstance(Instance, nullptr);
        Instance           = VK_NULL_HANDLE;
        ReclamationEnabled = PreviousReclamation;
    }
};
#undef FRONTIER_VULKAN_FUNCTIONS

//------------------------------------------------------------------------------------------------------------------------
//                                                     PRESENTATION LIFECYCLE
//------------------------------------------------------------------------------------------------------------------------

VulkanPresentation::VulkanPresentation() : Allocation(std::make_unique<PresentationAllocation>())
{
}
VulkanPresentation::~VulkanPresentation()
{
    Finalize();
}
void VulkanPresentation::Initialize(GLFWwindow *Window, bool MaterialRendering)
{
    Allocation->Initialize(Window, MaterialRendering);
}
void VulkanPresentation::Finalize() noexcept
{
    Allocation->Finalize();
}

VulkanRasterAccess VulkanPresentation::QueryRasterAccess() const
{
    const auto &Display = *Allocation;
    return {Display.Device, Display.PhysicalDevice,
        Resolve<PFN_vkGetDeviceProcAddr>(Display.Instance, "vkGetDeviceProcAddr"),
        Resolve<PFN_vkGetPhysicalDeviceMemoryProperties>(Display.Instance, "vkGetPhysicalDeviceMemoryProperties"),
        Resolve<PFN_vkGetPhysicalDeviceFormatProperties>(Display.Instance, "vkGetPhysicalDeviceFormatProperties"),
        Display.GetPhysicalDeviceProperties, Display.QueueCapabilities};
}
void VulkanPresentation::ConfigureRendering(std::function<void(VkCommandBuffer, uint64_t, uint64_t)> Construct,
                                           std::function<void()> Confirm)
{
    Allocation->ConstructGpu = std::move(Construct);
    Allocation->ConfirmGpu = std::move(Confirm);
}

bool VulkanPresentation::Reconfigure(int Width, int Height)
{
    auto &Display = *Allocation;
    if (Width <= 0 || Height <= 0)
        return false;
    if (Display.Rebuild || Width != Display.Main.Width || Height != Display.Main.Height)
    {
        RequireSuccess(Display.DeviceWaitIdle(Display.Device));
        ImGui_ImplVulkan_SetMinImageCount(2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(Display.Instance, Display.PhysicalDevice, Display.Device, &Display.Main,
                                               Display.QueueFamily, nullptr, Width, Height, 2, 0);
        Display.Main.FrameIndex = 0;
        Display.Rebuild         = false;
    }
    return true;
}

void VulkanPresentation::RenderMain(ImDrawData *Commands)
{
    auto &Display          = *Allocation;
    auto &Main             = Display.Main;
    Display.Submitted      = false;
    const auto &Semaphores = Main.FrameSemaphores[Main.SemaphoreIndex];
    const auto  Result =
        Display.AcquireNextImageKHR(Display.Device, Main.Swapchain, UINT64_MAX, Semaphores.ImageAcquiredSemaphore,
                                    VK_NULL_HANDLE, &Main.FrameIndex);
    if (Result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        Display.Rebuild = true;
        return;
    }
    if (Result == VK_SUBOPTIMAL_KHR)
        Display.Rebuild = true;
    else
        RequireSuccess(Result);
    static int TracedFrames = 0;
    const bool TraceFrame = TracedFrames < 4;
    if (TraceFrame) DiagnosticJournal::Append("FrameAcquire", "image=" + std::to_string(Main.FrameIndex));
    auto &Submission = Main.Frames[Main.FrameIndex];
    if (TraceFrame) DiagnosticJournal::Append("FrameWait", "image=" + std::to_string(Main.FrameIndex) +
        " serial=" + std::to_string(Display.ImageSerials.size() == Main.ImageCount ? Display.ImageSerials[Main.FrameIndex] : 0));
    RequireSuccess(Display.WaitForFences(Display.Device, 1, &Submission.Fence, VK_TRUE, UINT64_MAX));
    if (TraceFrame) DiagnosticJournal::Append("FrameFenced", "image=" + std::to_string(Main.FrameIndex));
    if (Display.ImageSerials.size() != Main.ImageCount)
    {
        // 📝 Reconfigure waits for device completion before replacing swapchain allocations.
        Display.CompletedSerial = Display.SubmissionSerial;
        Display.ImageSerials.assign(Main.ImageCount, 0);
    }
    Display.CompletedSerial = std::max(Display.CompletedSerial, Display.ImageSerials[Main.FrameIndex]);
    RequireSuccess(Display.ResetCommandPool(Display.Device, Submission.CommandPool, 0));
    VkCommandBufferBeginInfo Begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    Begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    RequireSuccess(Display.BeginCommandBuffer(Submission.CommandBuffer, &Begin));
    if (Display.ConstructGpu) Display.ConstructGpu(Submission.CommandBuffer, Display.CompletedSerial, Display.SubmissionSerial + 1);
    VkRenderPassBeginInfo Rasterization{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    Rasterization.renderPass        = Main.RenderPass;
    Rasterization.framebuffer       = Submission.Framebuffer;
    Rasterization.renderArea.extent = {static_cast<uint32_t>(Main.Width), static_cast<uint32_t>(Main.Height)};
    Rasterization.clearValueCount   = 1;
    Rasterization.pClearValues      = &Main.ClearValue;
    Display.CmdBeginRenderPass(Submission.CommandBuffer, &Rasterization, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(Commands, Submission.CommandBuffer);
    Display.CmdEndRenderPass(Submission.CommandBuffer);
    RequireSuccess(Display.EndCommandBuffer(Submission.CommandBuffer));
    constexpr VkPipelineStageFlags Wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo                   Submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    Submit.waitSemaphoreCount   = 1;
    Submit.pWaitSemaphores      = &Semaphores.ImageAcquiredSemaphore;
    Submit.pWaitDstStageMask    = &Wait;
    Submit.commandBufferCount   = 1;
    Submit.pCommandBuffers      = &Submission.CommandBuffer;
    Submit.signalSemaphoreCount = 1;
    Submit.pSignalSemaphores    = &Semaphores.RenderCompleteSemaphore;
    RequireSuccess(Display.ResetFences(Display.Device, 1, &Submission.Fence));
    RequireSuccess(Display.QueueSubmit(Display.Queue, 1, &Submit, Submission.Fence));
    Display.ImageSerials[Main.FrameIndex] = ++Display.SubmissionSerial;
    if (TraceFrame)
    {
        DiagnosticJournal::Append("FrameSubmit", "serial=" + std::to_string(Display.SubmissionSerial));
        ++TracedFrames;
    }
    if (Display.ConfirmGpu) Display.ConfirmGpu();
    Display.Submitted = true;
}

void VulkanPresentation::AwaitCompletion() noexcept
{
    if (Allocation && Allocation->Device && Allocation->DeviceWaitIdle)
        (void)Allocation->DeviceWaitIdle(Allocation->Device);
}

void VulkanPresentation::PresentMain()
{
    auto &Display = *Allocation;
    if (!Display.Submitted)
        return;
    auto            &Main = Display.Main;
    VkPresentInfoKHR Present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    Present.waitSemaphoreCount = 1;
    Present.pWaitSemaphores    = &Main.FrameSemaphores[Main.SemaphoreIndex].RenderCompleteSemaphore;
    Present.swapchainCount     = 1;
    Present.pSwapchains        = &Main.Swapchain;
    Present.pImageIndices      = &Main.FrameIndex;
    const auto Result          = Display.QueuePresentKHR(Display.Queue, &Present);
    if (Result == VK_ERROR_OUT_OF_DATE_KHR || Result == VK_SUBOPTIMAL_KHR)
        Display.Rebuild = true;
    else
        RequireSuccess(Result);
    Main.SemaphoreIndex = (Main.SemaphoreIndex + 1) % Main.SemaphoreCount;
    static int TracedPresents = 0;
    if (TracedPresents < 4)
    {
        DiagnosticJournal::Append("FramePresented", "serial=" + std::to_string(Display.SubmissionSerial));
        ++TracedPresents;
    }
    Display.Submitted   = false;
}
} // namespace Frontier
