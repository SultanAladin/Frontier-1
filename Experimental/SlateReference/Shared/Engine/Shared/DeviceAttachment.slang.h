//============================================================================================================================================
//                                                       DEVICEATTACHMENT.SLANG.H
//============================================================================================================================================
// 🧩 The nine handles a host receives when a device is created, and hands to whatever draws with it.
//
// 🔴 THIS WAS TWO STRUCTS: `DeviceOffering` in `SlateVulkan/Device/HostLifecycle` and
//    `InterfaceAttachment` in `SlateUI/Interface/InterfaceExchange`. Nine fields, same types, same
//    names, same order, and an `Attach` function whose entire body copied each one across:
//
//        Incoming.Instance = Offered.Instance;
//        Incoming.ScoredDevice = Offered.ScoredDevice;
//        ... seven more ...
//
// ⚠️ THE COPY WAS THE DEFECT, NOT THE DUPLICATION. Two structs that must agree field-for-field, in two
//    units, with a hand-written transfer between them, is the exact shape of the four defects recorded in
//    `HostDebtPlan.md` §4: a field added to one side and forgotten on the other compiles perfectly and
//    silently arrives as a null handle. Nothing in the type system says the two are the same thing,
//    because until now they were not.
//
// 📝 `Shared/` is the one place both a device unit and an interface unit may read. Both already include
//    `<vulkan/vulkan.h>` directly, so this moves no dependency; it only stops the two copies being able
//    to disagree. The two old names remain as aliases, because they read correctly at their call sites:
//    the device layer OFFERS, the interface layer receives an ATTACHMENT.

#pragma once

#include <cstdint>

#include <vulkan/vulkan.h>

namespace Slate
{

/// 🧩 Everything the interface layer needs in order to draw on the device the host created.
struct DeviceAttachment
{
    VkInstance        Instance                 = VK_NULL_HANDLE;         // [-] - the loaded instance
    VkPhysicalDevice  ScoredDevice             = VK_NULL_HANDLE;         // [-] - the device VendorClassifier won
    VkDevice          ActiveDevice             = VK_NULL_HANDLE;         // [-] - the created device
    VkQueue           GraphicsQueue            = VK_NULL_HANDLE;         // [-] - the one queue taken
    std::uint32_t     GraphicsFamilyIndex      = 0u;                     // [-] - the family that queue sits in
    VkFormat          ColourTargetFormat       = VK_FORMAT_UNDEFINED;    // [-] - format of DisplaySurface
    std::uint32_t     MinimumDisplayImageCount = 0u;                     // [-] - minimum requested of the chain
    std::uint32_t     DisplayImageCount        = 0u;                     // [-] - actual images the chain holds
    void*             NativeWindowSlot         = nullptr;                // [-] - WindowInterchange's handle
};

/// 📝 What the device layer hands out. Same type, and the name reads correctly where it is used.
using DeviceOffering = DeviceAttachment;

/// 📝 What the interface layer receives. Same type; see above.
using InterfaceAttachment = DeviceAttachment;

}   // namespace Slate
