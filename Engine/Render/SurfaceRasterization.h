/*==========================================================================================================================================
                                                         SURFACERASTERIZATION.H
==========================================================================================================================================*/
// 🧩 Shared Vulkan surface programs, resident geometry and independently sized offscreen targets with caller-owned submission.

#pragma once
#include "SurfaceGeometry.h"
#include "MatCapProjection.h"
#include "ScatteringSequence.h"
#include "SurfaceMaterial.h"
#include <memory>
#include <span>
#include <vulkan/vulkan.h>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     BORROWED DEVICE ACCESS
//------------------------------------------------------------------------------------------------------------------------

struct VulkanRasterAccess
{
    VkDevice                                  Device        = VK_NULL_HANDLE;
    VkPhysicalDevice                          PhysicalDevice = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr                    ResolveDevice = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties    QueryMemory   = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties    QueryFormat   = nullptr;
    PFN_vkGetPhysicalDeviceProperties          QueryLimits   = nullptr;
    VkQueueFlags                              QueueCapabilities = 0;
};

struct RasterShader
{
    std::span<const uint32_t> Words;
    const char              *Entry = nullptr;
};

struct SurfaceRasterShaders
{
    RasterShader SurfaceVertex;
    RasterShader SurfaceFragment;
    RasterShader EdgeVertex;
    RasterShader EdgeFragment;
    RasterShader MatCapVertex{};
    RasterShader MatCapFragment{};
    RasterShader PreviewPbrVertex{};
    RasterShader PreviewPbrFragment{};
    RasterShader MaterialVertex{};
    RasterShader MaterialFragment{};
};

class SurfaceRasterPrograms;
class SurfaceGeometryResidence;
class SurfaceScatteringResidence;
class SurfaceMaterialResidence;
class SurfaceTextureResidence;
using SurfaceMatCap = SurfaceTextureResidence;

//------------------------------------------------------------------------------------------------------------------------
//                                                     INDEPENDENT IMAGE TARGET
//------------------------------------------------------------------------------------------------------------------------

// ⚠️ All three units borrow one device. The submission owner serializes a single graphics queue; no fences,
// semaphores or idle waits are created here.
// Serialize calls through the submission owner. Record only after this target's completion fence; retire after
// every submission reading it completes. Geometry/extent are immutable: replace and retire at that boundary.
// Targets retain shared programs/geometry; neither source scope ending nor Finalize invalidates retained targets.
class SurfaceRasterization
{
public:
    SurfaceRasterization();
    ~SurfaceRasterization();
    SurfaceRasterization(const SurfaceRasterization &) = delete;
    SurfaceRasterization &operator=(const SurfaceRasterization &) = delete;
    // 📝 Move transfers GPU ownership for target replacement (preview resize); the source keeps a fresh
    // uninitialized allocation so it can be re-initialized in place, and destruction stays safe.
    SurfaceRasterization(SurfaceRasterization &&Source) noexcept;
    void Initialize(
        const SurfaceRasterPrograms    &Programs,
        const SurfaceGeometryResidence &Geometry,
        uint32_t                        Width,
        uint32_t                        Height);
    void ConstructCommands(VkCommandBuffer Commands, const SurfaceProjection &Projection);
    void ConstructCommands(
        VkCommandBuffer         Commands,
        const MatCapProjection &Projection,
        const SurfaceMatCap    &MatCap);
    void ConstructCommands(
        VkCommandBuffer                    Commands,
        const SurfaceNormalProjection     &Projection,
        const SurfaceTextureResidence     &Albedo,
        const SurfaceScatteringResidence  &Scattering);
    void ConstructCommands(
        VkCommandBuffer                   Commands,
        const SurfaceNormalProjection    &Projection,
        const SurfaceMaterialResidence   &Material,
        const SurfaceScatteringResidence &Parameters);
    // 📝 Confirm immediately after the queue accepts this recording, before constructing other first-use targets.
    // Aborted recordings leave the shared geometry transfer pending for retry; submission is not a fence completion.
    void ConfirmSubmission() noexcept;
    void ConfigureTransparentBackground(bool Enabled) noexcept;
    // 📝 Restricts subsequent recordings to one index sub-range of the shared geometry (count 0 = all).
    void ConfigureIndexRange(uint32_t FirstIndex, uint32_t IndexCount) noexcept;
    void Finalize() noexcept;
    [[nodiscard]] VkImage QueryImage() const noexcept;
    [[nodiscard]] VkImageView QueryImageView() const noexcept;
    [[nodiscard]] VkExtent2D QueryExtent() const noexcept;
    // 📝 Counts accepted transfer submissions; a separate completion fence establishes GPU readiness.
    [[nodiscard]] uint64_t QueryGeometryUploadCount() const noexcept;

private:
    friend class SurfaceRasterPrograms;
    friend class SurfaceGeometryResidence;
    friend class SurfaceTextureResidence;
    friend class SurfaceScatteringResidence;
    friend class SurfaceMaterialResidence;
    struct RasterAllocation;
    std::unique_ptr<RasterAllocation> Allocation;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED DEVICE PROGRAMS
//------------------------------------------------------------------------------------------------------------------------

class SurfaceRasterPrograms
{
public:
    SurfaceRasterPrograms();
    ~SurfaceRasterPrograms();
    SurfaceRasterPrograms(const SurfaceRasterPrograms &) = delete;
    SurfaceRasterPrograms &operator=(const SurfaceRasterPrograms &) = delete;
    void Initialize(const VulkanRasterAccess &Access, const SurfaceRasterShaders &Shaders);
    // 📝 CPU-side equirect studio archive (one array layer per environment); material residences
    // built afterwards upload their own copy at set 0 binding 3 through the normal transfer path.
    void ConfigureEnvironment(const MaterialTexturePyramid &Environment);
    void Finalize() noexcept;

private:
    friend class SurfaceRasterization;
    friend class SurfaceGeometryResidence;
    friend class SurfaceTextureResidence;
    friend class SurfaceScatteringResidence;
    friend class SurfaceMaterialResidence;
    std::shared_ptr<SurfaceRasterization::RasterAllocation> Allocation;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED RESIDENT GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

class SurfaceGeometryResidence
{
public:
    SurfaceGeometryResidence();
    ~SurfaceGeometryResidence();
    SurfaceGeometryResidence(const SurfaceGeometryResidence &) = delete;
    SurfaceGeometryResidence &operator=(const SurfaceGeometryResidence &) = delete;
    void Initialize(const VulkanRasterAccess &Access, const SurfaceGeometry &Geometry);
    void Finalize() noexcept;

private:
    friend class SurfaceRasterization;
    std::shared_ptr<SurfaceRasterization::RasterAllocation> Allocation;
};
//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED RESIDENT MATCAP TEXTURE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One immutable lookup image/set serves any number of targets; programs share one fixed linear-clamp sampler.
// Replacements obey the same completion fence contract.
class SurfaceTextureResidence
{
public:
    SurfaceTextureResidence();
    ~SurfaceTextureResidence();
    SurfaceTextureResidence(const SurfaceTextureResidence &) = delete;
    SurfaceTextureResidence &operator=(const SurfaceTextureResidence &) = delete;
    void Initialize(const SurfaceRasterPrograms &Programs, const MatCapImage &Image);
    void Finalize() noexcept;
    [[nodiscard]] uint64_t QueryUploadCount() const noexcept;

private:
    friend class SurfaceRasterization;
    std::shared_ptr<SurfaceRasterization::RasterAllocation> Allocation;
};
//------------------------------------------------------------------------------------------------------------------------
//                                                     IMMUTABLE SCATTERING PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

class SurfaceScatteringResidence
{
public:
    SurfaceScatteringResidence();
    ~SurfaceScatteringResidence();
    SurfaceScatteringResidence(const SurfaceScatteringResidence &) = delete;
    SurfaceScatteringResidence &operator=(const SurfaceScatteringResidence &) = delete;
    void Initialize(const SurfaceRasterPrograms &Programs, const PreviewScattering &Scattering);
    void Initialize(const SurfaceRasterPrograms &Programs, const MaterialScattering &Scattering);
    void Finalize() noexcept;

private:
    friend class SurfaceRasterization;
    std::shared_ptr<SurfaceRasterization::RasterAllocation> Allocation;
};
//------------------------------------------------------------------------------------------------------------------------
//                                                     IMMUTABLE MATERIAL TEXTURES
//------------------------------------------------------------------------------------------------------------------------

class SurfaceMaterialResidence
{
public:
    SurfaceMaterialResidence();
    ~SurfaceMaterialResidence();
    SurfaceMaterialResidence(const SurfaceMaterialResidence &) = delete;
    SurfaceMaterialResidence &operator=(const SurfaceMaterialResidence &) = delete;
    void Initialize(const SurfaceRasterPrograms &Programs,const std::array<MaterialTexturePyramid,4> &Textures);
    void Initialize(const SurfaceRasterPrograms &Programs,const std::array<MaterialTexturePyramid,4> &Textures,
                    const std::array<MaterialTexturePyramid,6> &AuthoringMaps);
    void Finalize() noexcept;
    [[nodiscard]] uint64_t QueryUploadCount() const noexcept;

private:
    friend class SurfaceRasterization;
    std::shared_ptr<SurfaceRasterization::RasterAllocation> Allocation;
};
} // namespace Frontier
