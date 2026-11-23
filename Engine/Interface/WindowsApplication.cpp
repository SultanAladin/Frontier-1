/*==========================================================================================================================================
                                                               WINDOWSAPPLICATION.CPP
==========================================================================================================================================*/
// 🧩 Share native window, input, UI presentation and failure handling without application-specific panel dependencies.

#include "WindowsApplication.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <algorithm>
#include <memory>
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
#if FRONTIER_UI_VULKAN
#include "Engine/Interface/VulkanPresentation.h"
#include <imgui_impl_vulkan.h>
#else
// 📝 The DX11 adapter is Windows-only; it is dead code for every target in this repository, which
// all build FRONTIER_UI_VULKAN=1, but stays buildable for the standalone FrontierUIDX11 exhibit.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#endif
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <iostream>

namespace
{
//------------------------------------------------------------------------------------------------------------------------
//                                                    DISPLAY RESOURCES
//------------------------------------------------------------------------------------------------------------------------

#if !FRONTIER_UI_VULKAN
struct DisplayResources
{
    ID3D11Device           *Device    = nullptr;
    ID3D11DeviceContext    *Context   = nullptr;
    IDXGISwapChain         *SwapChain = nullptr;
    ID3D11RenderTargetView *Target    = nullptr;

    bool InterfaceReady = false;
    ~DisplayResources()
    {
        if (InterfaceReady) ImGui_ImplDX11_Shutdown();
        if (Target)
            Target->Release();
        if (SwapChain)
            SwapChain->Release();
        if (Context)
            Context->Release();
        if (Device)
            Device->Release();
    }

    bool ConstructTarget()
    {
        ID3D11Texture2D *BackBuffer = nullptr;
        if (FAILED(SwapChain->GetBuffer(0, IID_PPV_ARGS(&BackBuffer))))
            return false;
        const auto Result = Device->CreateRenderTargetView(BackBuffer, nullptr, &Target);
        BackBuffer->Release();
        return SUCCEEDED(Result);
    }

    bool Initialize(HWND Window)
    {
        DXGI_SWAP_CHAIN_DESC Description{};
        Description.BufferCount       = 2;
        Description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        Description.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        Description.OutputWindow      = Window;
        Description.SampleDesc.Count  = 1;
        Description.Windowed          = TRUE;
        Description.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;
        D3D_FEATURE_LEVEL           Level;
        constexpr D3D_FEATURE_LEVEL Levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        auto                        Result =
            D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, Levels, 2, D3D11_SDK_VERSION,
                                          &Description, &SwapChain, &Device, &Level, &Context);
        if (Result == DXGI_ERROR_UNSUPPORTED)
            Result =
                D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, Levels, 2, D3D11_SDK_VERSION,
                                              &Description, &SwapChain, &Device, &Level, &Context);
        if (FAILED(Result))
            return false;
        IDXGIFactory *Factory = nullptr;
        if (SUCCEEDED(SwapChain->GetParent(IID_PPV_ARGS(&Factory))))
        {
            Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
            Factory->Release();
        }
        return ConstructTarget();
    }

    bool Reconfigure(int Width, int Height)
    {
        Context->OMSetRenderTargets(0, nullptr, nullptr);
        if (Target)
        {
            Target->Release();
            Target = nullptr;
        }
        return SUCCEEDED(SwapChain->ResizeBuffers(0, Width, Height, DXGI_FORMAT_UNKNOWN, 0)) && ConstructTarget();
    }
};

#endif

struct HostLifetime
{
    GLFWwindow *Window        = nullptr;
    bool        ContextReady  = false;
    bool        PlatformReady = false;
    ~HostLifetime()
    {
        // 📝 Last-resort shutdown guard: ImGui::DestroyContext() walks its own internal dock node
        // graph to release it, and a docking state corrupted earlier in the session (surfaced
        // separately as a "Cyclic, shared or out-of-range layout region" workspace-layout-save
        // rejection) can turn that walk into an unbounded allocation loop that throws std::bad_alloc
        // here — well after every application resource has already been finalized, with nothing left
        // that depends on this teardown completing. Swallow any such failure instead of letting it
        // escape as a fatal "Severe failure" dialog on an application that is already closing.
        try
        {
            if (PlatformReady)
                ImGui_ImplGlfw_Shutdown();
            if (ContextReady)
                ImGui::DestroyContext();
            if (Window)
                glfwDestroyWindow(Window);
            glfwTerminate();
        }
        catch (const std::exception &Failure)
        {
            Frontier::DiagnosticJournal::Append("ShutdownTeardownError", Failure.what());
        }
        catch (...)
        {
            Frontier::DiagnosticJournal::Append("ShutdownTeardownError", "Unknown platform teardown failure");
        }
    }
};

void ReportGlfwError(int Code, const char *Description)
{
    Frontier::DiagnosticJournal::Append("GLFW", std::to_string(Code) + ": " + (Description ? Description : "unknown"));
    std::cerr << "GLFW " << Code << ": " << Description << '\n';
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      WINDOWS HOST
//------------------------------------------------------------------------------------------------------------------------

namespace Frontier
{
namespace
{
// 📝 Surface a severe failure to the user: a native message box on Windows, stderr (alongside the
// DiagnosticJournal entry the caller already wrote) on every other platform.
void ReportSevereFailure(const char *Title, const std::string &Message) noexcept
{
#ifdef _WIN32
    wchar_t Description[2048]{};
    const auto Length = static_cast<int>(std::min<size_t>(Message.size(), 2047));
    const auto Decoded = MultiByteToWideChar(CP_UTF8, 0, Message.c_str(), Length, Description, 2047);
    const std::wstring WideTitle(Title, Title + std::strlen(Title));
    MessageBoxW(nullptr, Decoded > 0 ? Description : L"An application failure prevented normal operation.",
                WideTitle.c_str(), MB_OK | MB_ICONERROR | MB_TASKMODAL);
#else
    std::cerr << Title << ": " << Message << '\n';
#endif
}
int ConstructApplicationWindow(const ApplicationWindowCallbacks &Callbacks)
{
    DiagnosticJournal::Append("Window", "Creating " + Callbacks.Title);
#ifdef FRONTIER_BUILD_REVISION
    DiagnosticJournal::Append("Build", FRONTIER_BUILD_REVISION);
#endif
    glfwSetErrorCallback(ReportGlfwError);
    if (!glfwInit()) throw std::runtime_error("GLFW platform initialization failed.");
    HostLifetime Lifetime;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow *Window = glfwCreateWindow(1280, 900, Callbacks.Title.c_str(), nullptr, nullptr);
    if (!Window) throw std::runtime_error("Application window creation failed.");
    Lifetime.Window = Window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    Lifetime.ContextReady = true;
    auto &IO = ImGui::GetIO();
    IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    IO.IniFilename = nullptr;
#if FRONTIER_UI_VULKAN
    VulkanPresentation Display;
    Lifetime.PlatformReady = ImGui_ImplGlfw_InitForVulkan(Window, true);
    if (!Lifetime.PlatformReady) throw std::runtime_error("GLFW Vulkan input initialization failed.");
    Display.Initialize(Window, Callbacks.MaterialRendering);
#else
    DisplayResources Display;
    if (!Display.Initialize(glfwGetWin32Window(Window))) throw std::runtime_error("DX11 exhibit initialization failed.");
    Lifetime.PlatformReady = ImGui_ImplGlfw_InitForOther(Window, true);
    Display.InterfaceReady = ImGui_ImplDX11_Init(Display.Device, Display.Context);
    if (!Lifetime.PlatformReady || !Display.InterfaceReady) throw std::runtime_error("DX11 interface initialization failed.");
#endif
    const auto FinalizeApplication = [&]() noexcept
    {
        try
        {
#if FRONTIER_UI_VULKAN
            Display.AwaitCompletion();
#else
            if (Display.Context) Display.Context->Flush();
#endif
            DiagnosticJournal::Append("Shutdown", "GPU idle confirmed");
        }
        catch (const std::exception &Failure)
        {
            DiagnosticJournal::Append("SEVERE", std::string("GPU shutdown: ") + Failure.what());
            ReportSevereFailure("Frontier - Severe GPU shutdown failure", Failure.what());
        }
        try
        {
            if (Callbacks.Finalize) Callbacks.Finalize();
            DiagnosticJournal::Append("Shutdown", "Application resources finalized");
        }
        catch (...)
        {
            DiagnosticJournal::Append("SEVERE", "Exception while finalizing application resources");
            ReportSevereFailure("Frontier - Severe failure",
                "Application shutdown failed. Collect the DiagnosticJournal log (see Frontier/Logs).");
        }
    };
    struct ApplicationReclamation
    {
        const decltype(FinalizeApplication) &Finalize;
        ~ApplicationReclamation() { Finalize(); }
    } Reclamation{FinalizeApplication};
    DiagnosticJournal::Append("Startup", "Platform/window/ImGui presentation initialized");
    if (Callbacks.Initialize) Callbacks.Initialize(Window);
    DiagnosticJournal::Append("Startup", "Application content initialized");
#if FRONTIER_UI_VULKAN
    if (Callbacks.InitializeVulkan) Callbacks.InitializeVulkan(Display);
#endif
#if !FRONTIER_UI_VULKAN
    int PreviousWidth = 0, PreviousHeight = 0;
#endif
    while (!glfwWindowShouldClose(Window))
    {
        glfwPollEvents();
        static int TracedLoop = 0;
        if (TracedLoop < 8)
        {
            DiagnosticJournal::Append("FrameLoop", "frame=" + std::to_string(TracedLoop));
            ++TracedLoop;
        }
        if (Callbacks.ConfigurePlatform) Callbacks.ConfigurePlatform();
        int Width = 0, Height = 0;
        glfwGetFramebufferSize(Window, &Width, &Height);
        const bool MainVisible = Width > 0 && Height > 0;
        if (!MainVisible && (!(IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) || ImGui::GetPlatformIO().Viewports.Size <= 1))
        {
            glfwWaitEventsTimeout(.05);
            continue;
        }
        if (MainVisible &&
#if FRONTIER_UI_VULKAN
            true
#else
            (Width != PreviousWidth || Height != PreviousHeight)
#endif
        )
        {
            if (!Display.Reconfigure(Width, Height)) throw std::runtime_error("Application display resize failed.");
#if !FRONTIER_UI_VULKAN
            PreviousWidth = Width;
            PreviousHeight = Height;
#endif
        }
        float DensityX = 1, DensityY = 1;
        glfwGetWindowContentScale(Window, &DensityX, &DensityY);
        ImGui::GetStyle().FontScaleDpi = DensityY;
        if (Callbacks.PrepareInterface) Callbacks.PrepareInterface();
#if FRONTIER_UI_VULKAN
        ImGui_ImplVulkan_NewFrame();
#else
        ImGui_ImplDX11_NewFrame();
#endif
        ImGui_ImplGlfw_NewFrame();
        if (Callbacks.RecoverWindows) Callbacks.RecoverWindows();
        ImGui::NewFrame();
        if (Callbacks.ConstructInterface) Callbacks.ConstructInterface(MainVisible);
        ImGui::Render();
        static int TracedBuilt = 0;
        if (TracedBuilt < 8)
        {
            DiagnosticJournal::Append("FrameBuilt", "frame=" + std::to_string(TracedBuilt));
            ++TracedBuilt;
        }
        if (Callbacks.AfterRender) Callbacks.AfterRender();
        if (MainVisible)
        {
#if FRONTIER_UI_VULKAN
            static int TracedInterface = 0;
            if (TracedInterface < 8)
            {
                DiagnosticJournal::Append("FrameUI", "frame=" + std::to_string(TracedInterface));
                ++TracedInterface;
            }
            Display.RenderMain(ImGui::GetDrawData());
#else
            constexpr float Background[] = {.065f,.065f,.07f,1};
            Display.Context->OMSetRenderTargets(1, &Display.Target, nullptr);
            Display.Context->ClearRenderTargetView(Display.Target, Background);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
#endif
        }
        if (IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
        if (MainVisible)
        {
#if FRONTIER_UI_VULKAN
            Display.PresentMain();
#else
            if (FAILED(Display.SwapChain->Present(1, 0))) throw std::runtime_error("DX11 exhibit presentation failed.");
#endif
        }
    }
    return 0;
}
} // namespace

int RunWindowsApplication(const ApplicationWindowCallbacks &Callbacks)
{
    try { return ConstructApplicationWindow(Callbacks); }
    catch (const std::exception &Failure)
    {
        DiagnosticJournal::Append("SEVERE", Failure.what());
        std::cerr << "Severe application failure: " << Failure.what() << '\n';
        const auto Message = std::string(Failure.what()) + "\n\nDiagnostic file: " + DiagnosticJournal::EncodePath(DiagnosticJournal::QueryFilename());
        ReportSevereFailure("Frontier - Severe failure", Message);
    }
    catch (...)
    {
        DiagnosticJournal::Append("SEVERE", "Unknown application exception");
        ReportSevereFailure("Frontier - Severe failure", "An unknown application failure prevented normal operation.");
    }
    return 1;
}
} // namespace Frontier
