/*==========================================================================================================================================
                                                      MATERIALTHUMBNAILATLAS.CPP
==========================================================================================================================================*/
// 🧩 Copy shared material rasterization into one GPU atlas; duplicate edge texels and synchronize transfer/sampling.

#include "MaterialThumbnailAtlas.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace Frontier
{
namespace
{
void RequireAtlasSuccess(VkResult Result)
{
    if (Result != VK_SUCCESS) throw std::runtime_error("Vulkan thumbnail atlas allocation failed.");
}
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DEVICE ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

struct MaterialThumbnailAtlas::AtlasAllocation
{
    VulkanRasterAccess Access;
    VkImage Image = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkBuffer Staging = VK_NULL_HANDLE;
    VkDeviceMemory StagingMemory = VK_NULL_HANDLE;
    VkBuffer Readback = VK_NULL_HANDLE;
    VkDeviceMemory ReadbackMemory = VK_NULL_HANDLE;
    bool ReadbackRecorded = false;
    uint64_t ReadbackSubmission = 0;
    uint32_t Tile = 0, Columns = 0, Extent = 0;
    bool Submitted = false, Recorded = false;
#define ATLAS_FUNCTIONS(F) F(CreateImage) F(DestroyImage) F(GetImageMemoryRequirements) F(AllocateMemory) \
    F(FreeMemory) F(BindImageMemory) F(CreateImageView) F(DestroyImageView) F(CmdPipelineBarrier) \
    F(CmdCopyImage) F(CmdClearColorImage) F(CreateBuffer) F(DestroyBuffer) F(GetBufferMemoryRequirements) \
    F(BindBufferMemory) F(MapMemory) F(UnmapMemory) F(CmdCopyBufferToImage) F(CmdCopyImageToBuffer)
#define ATLAS_DECLARE(Name) PFN_vk##Name Name = nullptr;
    ATLAS_FUNCTIONS(ATLAS_DECLARE)
#undef ATLAS_DECLARE
    ~AtlasAllocation()
    {
        if (View) DestroyImageView(Access.Device, View, nullptr);
        if (Image) DestroyImage(Access.Device, Image, nullptr);
        if (Memory) FreeMemory(Access.Device, Memory, nullptr);
        if (Staging) DestroyBuffer(Access.Device, Staging, nullptr);
        if (StagingMemory) FreeMemory(Access.Device, StagingMemory, nullptr);
        if (Readback) DestroyBuffer(Access.Device, Readback, nullptr);
        if (ReadbackMemory) FreeMemory(Access.Device, ReadbackMemory, nullptr);
    }
    void ConstructTransition(VkCommandBuffer Commands, VkImage Target, VkImageLayout Before, VkImageLayout After,
                 VkAccessFlags SourceAccess, VkAccessFlags DestinationAccess,
                 VkPipelineStageFlags Source, VkPipelineStageFlags Destination)
    {
        VkImageMemoryBarrier Transition{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        Transition.image = Target;
        Transition.oldLayout = Before;
        Transition.newLayout = After;
        Transition.srcAccessMask = SourceAccess;
        Transition.dstAccessMask = DestinationAccess;
        Transition.srcQueueFamilyIndex = Transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        CmdPipelineBarrier(Commands, Source, Destination, 0, 0, nullptr, 0, nullptr, 1, &Transition);
    }
};

MaterialThumbnailAtlas::MaterialThumbnailAtlas() = default;
MaterialThumbnailAtlas::~MaterialThumbnailAtlas() = default;
void MaterialThumbnailAtlas::Finalize() noexcept { Allocation.reset(); }
void MaterialThumbnailAtlas::Initialize(const VulkanRasterAccess &Access, uint32_t TileExtent, uint32_t Columns)
{
    if (Allocation || !Access.Device || !Access.PhysicalDevice || !Access.ResolveDevice || !Access.QueryMemory || !Access.QueryLimits ||
        !Access.QueryFormat || TileExtent < 16 || TileExtent > 512 || !Columns || Columns > 16)
        throw std::runtime_error("Invalid or already initialized thumbnail atlas.");
    auto Resident = std::make_unique<AtlasAllocation>();
    Resident->Access = Access;
#define ATLAS_LOAD(Name) Resident->Name = reinterpret_cast<PFN_vk##Name>(Access.ResolveDevice(Access.Device, "vk" #Name)); \
    if (!Resident->Name) throw std::runtime_error("Missing thumbnail atlas Vulkan entry point: " #Name);
    ATLAS_FUNCTIONS(ATLAS_LOAD)
#undef ATLAS_LOAD
#undef ATLAS_FUNCTIONS
    Resident->Tile = TileExtent;
    Resident->Columns = Columns;
    Resident->Extent = (TileExtent + 2) * Columns;
    VkPhysicalDeviceProperties Limits{};
    Access.QueryLimits(Access.PhysicalDevice, &Limits);
    VkFormatProperties Format{};
    Access.QueryFormat(Access.PhysicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &Format);
    constexpr auto Required = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (Resident->Extent > Limits.limits.maxImageDimension2D || (Format.optimalTilingFeatures & Required) != Required)
        throw std::runtime_error("Device cannot hold the requested bounded thumbnail atlas.");
    VkImageCreateInfo Image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    Image.imageType = VK_IMAGE_TYPE_2D;
    Image.format = VK_FORMAT_R8G8B8A8_UNORM;
    Image.extent = {Resident->Extent, Resident->Extent, 1};
    Image.mipLevels = Image.arrayLayers = 1;
    Image.samples = VK_SAMPLE_COUNT_1_BIT;
    Image.tiling = VK_IMAGE_TILING_OPTIMAL;
    Image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    RequireAtlasSuccess(Resident->CreateImage(Access.Device, &Image, nullptr, &Resident->Image));
    VkMemoryRequirements Requirements{};
    Resident->GetImageMemoryRequirements(Access.Device, Resident->Image, &Requirements);
    VkPhysicalDeviceMemoryProperties Memory{};
    Access.QueryMemory(Access.PhysicalDevice, &Memory);
    uint32_t Index = 0;
    for (; Index < Memory.memoryTypeCount; ++Index)
        if ((Requirements.memoryTypeBits & (1u << Index)) &&
            (Memory.memoryTypes[Index].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
    if (Index == Memory.memoryTypeCount) throw std::runtime_error("No device-local atlas memory.");
    VkMemoryAllocateInfo Storage{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    Storage.allocationSize = Requirements.size;
    Storage.memoryTypeIndex = Index;
    RequireAtlasSuccess(Resident->AllocateMemory(Access.Device, &Storage, nullptr, &Resident->Memory));
    RequireAtlasSuccess(Resident->BindImageMemory(Access.Device, Resident->Image, Resident->Memory, 0));
    VkImageViewCreateInfo View{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    View.image = Resident->Image;
    View.viewType = VK_IMAGE_VIEW_TYPE_2D;
    View.format = Image.format;
    View.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    RequireAtlasSuccess(Resident->CreateImageView(Access.Device, &View, nullptr, &Resident->View));
    VkBufferCreateInfo Staging{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    Staging.size = VkDeviceSize(Resident->Extent) * Resident->Extent * 4;
    Staging.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    Staging.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    RequireAtlasSuccess(Resident->CreateBuffer(Access.Device, &Staging, nullptr, &Resident->Staging));
    VkMemoryRequirements StagingRequirements{};
    Resident->GetBufferMemoryRequirements(Access.Device, Resident->Staging, &StagingRequirements);
    uint32_t StagingIndex = 0;
    for (; StagingIndex < Memory.memoryTypeCount; ++StagingIndex)
        if ((StagingRequirements.memoryTypeBits & (1u << StagingIndex)) &&
            (Memory.memoryTypes[StagingIndex].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
    if (StagingIndex == Memory.memoryTypeCount) throw std::runtime_error("No host-visible atlas staging memory.");
    VkMemoryAllocateInfo StagingStorage{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    StagingStorage.allocationSize = StagingRequirements.size;
    StagingStorage.memoryTypeIndex = StagingIndex;
    RequireAtlasSuccess(Resident->AllocateMemory(Access.Device, &StagingStorage, nullptr, &Resident->StagingMemory));
    RequireAtlasSuccess(Resident->BindBufferMemory(Access.Device, Resident->Staging, Resident->StagingMemory, 0));
    Allocation = std::move(Resident);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     COPY AND SAMPLING
//------------------------------------------------------------------------------------------------------------------------

void MaterialThumbnailAtlas::ConstructCopy(VkCommandBuffer Commands, SurfaceRasterization &Source, uint32_t Slot)
{
    if (!Allocation || Allocation->Recorded || !Commands || Slot >= QueryCapacity() || Source.QueryExtent().width != Allocation->Tile ||
        Source.QueryExtent().height != Allocation->Tile || !Source.QueryImage())
        throw std::runtime_error("Thumbnail source/slot does not fit the atlas.");
    auto &Atlas = *Allocation;
    Atlas.Recorded = true;
    Atlas.ConstructTransition(Commands, Atlas.Image,
        Atlas.Submitted ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, Atlas.Submitted ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_TRANSFER_WRITE_BIT, Atlas.Submitted ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (!Atlas.Submitted)
    {
        const VkClearColorValue Clear{{.055f, .055f, .06f, 1}};
        const VkImageSubresourceRange Range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Atlas.CmdClearColorImage(Commands, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &Clear, 1, &Range);
        // 📝 Clear and tile writes are separate transfer operations on the same image.
        Atlas.ConstructTransition(Commands, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    Atlas.ConstructTransition(Commands, Source.QueryImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    const int32_t X = int32_t((Slot % Atlas.Columns) * (Atlas.Tile + 2));
    const int32_t Y = int32_t((Slot / Atlas.Columns) * (Atlas.Tile + 2));
    std::array<VkImageCopy, 9> Copies{};
    size_t Count = 0;
    const auto Copy = [&](int32_t SourceX, int32_t SourceY, int32_t TargetX, int32_t TargetY, uint32_t Width, uint32_t Height)
    {
        auto &Region = Copies[Count++];
        Region.srcSubresource = Region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        Region.srcOffset = {SourceX, SourceY, 0};
        Region.dstOffset = {X + TargetX, Y + TargetY, 0};
        Region.extent = {Width, Height, 1};
    };
    const int32_t Last = int32_t(Atlas.Tile - 1), Edge = int32_t(Atlas.Tile + 1);
    Copy(0, 0, 1, 1, Atlas.Tile, Atlas.Tile);
    Copy(0, 0, 0, 1, 1, Atlas.Tile);
    Copy(Last, 0, Edge, 1, 1, Atlas.Tile);
    Copy(0, 0, 1, 0, Atlas.Tile, 1);
    Copy(0, Last, 1, Edge, Atlas.Tile, 1);
    Copy(0, 0, 0, 0, 1, 1);
    Copy(Last, 0, Edge, 0, 1, 1);
    Copy(0, Last, 0, Edge, 1, 1);
    Copy(Last, Last, Edge, Edge, 1, 1);
    Atlas.CmdCopyImage(Commands, Source.QueryImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Atlas.Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, uint32_t(Count), Copies.data());
    Atlas.ConstructTransition(Commands, Source.QueryImage(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    Atlas.ConstructTransition(Commands, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void MaterialThumbnailAtlas::StagePixels(std::span<const uint8_t> Rgba)
{
    if (!Allocation || Rgba.size() != size_t(Allocation->Extent) * Allocation->Extent * 4)
        throw std::runtime_error("Atlas stage payload must hold one RGBA8 texel per atlas pixel.");
    auto &Atlas = *Allocation;
    void *Mapped = nullptr;
    RequireAtlasSuccess(Atlas.MapMemory(Atlas.Access.Device, Atlas.StagingMemory, 0, VK_WHOLE_SIZE, 0, &Mapped));
    // 📝 Coherent host memory carries the payload without an explicit flush.
    std::memcpy(Mapped, Rgba.data(), Rgba.size());
    Atlas.UnmapMemory(Atlas.Access.Device, Atlas.StagingMemory);
}

void MaterialThumbnailAtlas::ConstructUpload(VkCommandBuffer Commands)
{
    if (!Allocation || Allocation->Recorded || !Commands) throw std::runtime_error("Atlas upload is unavailable in the current state.");
    auto &Atlas = *Allocation;
    Atlas.Recorded = true;
    Atlas.ConstructTransition(Commands, Atlas.Image,
        Atlas.Submitted ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, Atlas.Submitted ? VK_ACCESS_SHADER_READ_BIT : 0,
        VK_ACCESS_TRANSFER_WRITE_BIT, Atlas.Submitted ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy Region{};
    Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    Region.imageExtent = {Atlas.Extent, Atlas.Extent, 1};
    Atlas.CmdCopyBufferToImage(Commands, Atlas.Staging, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
    Atlas.ConstructTransition(Commands, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}
void MaterialThumbnailAtlas::ConstructReadback(VkCommandBuffer Commands, uint64_t SubmissionSerial)
{
    if (!Allocation || !Commands || Allocation->ReadbackRecorded) throw std::runtime_error("Thumbnail atlas readback is unavailable in the current state.");
    auto &Atlas = *Allocation;
    if (!Atlas.Readback)
    {
        VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        Creation.size = VkDeviceSize(Atlas.Extent) * Atlas.Extent * 4;
        Creation.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        Creation.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        RequireAtlasSuccess(Atlas.CreateBuffer(Atlas.Access.Device, &Creation, nullptr, &Atlas.Readback));
        VkMemoryRequirements Requirements{};
        Atlas.GetBufferMemoryRequirements(Atlas.Access.Device, Atlas.Readback, &Requirements);
        VkPhysicalDeviceMemoryProperties Memory{};
        Atlas.Access.QueryMemory(Atlas.Access.PhysicalDevice, &Memory);
        uint32_t Index = 0;
        for (; Index < Memory.memoryTypeCount; ++Index)
            if ((Requirements.memoryTypeBits & (1u << Index)) &&
                (Memory.memoryTypes[Index].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
        if (Index == Memory.memoryTypeCount) throw std::runtime_error("No host-visible atlas readback memory.");
        VkMemoryAllocateInfo Storage{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        Storage.allocationSize = Requirements.size;
        Storage.memoryTypeIndex = Index;
        RequireAtlasSuccess(Atlas.AllocateMemory(Atlas.Access.Device, &Storage, nullptr, &Atlas.ReadbackMemory));
        RequireAtlasSuccess(Atlas.BindBufferMemory(Atlas.Access.Device, Atlas.Readback, Atlas.ReadbackMemory, 0));
    }
    // 📝 The atlas is only ever left in SHADER_READ_ONLY_OPTIMAL between operations (every write-copy and
    // upload ends there); this is purely additive and never touches the single-write-per-submission state.
    Atlas.ConstructTransition(Commands, Atlas.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy Region{};
    Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    Region.imageExtent = {Atlas.Extent, Atlas.Extent, 1};
    Atlas.CmdCopyImageToBuffer(Commands, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Atlas.Readback, 1, &Region);
    Atlas.ConstructTransition(Commands, Atlas.Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    VkMemoryBarrier Host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    Host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    Host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    Atlas.CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &Host, 0, nullptr, 0, nullptr);
    Atlas.ReadbackRecorded = true;
    Atlas.ReadbackSubmission = SubmissionSerial;
}
bool MaterialThumbnailAtlas::QueryReadbackReady(uint64_t CompletedSerial) const noexcept
{
    return Allocation && Allocation->ReadbackRecorded && Allocation->ReadbackSubmission <= CompletedSerial;
}
std::vector<uint8_t> MaterialThumbnailAtlas::RetrieveReadback(uint64_t CompletedSerial)
{
    if (!QueryReadbackReady(CompletedSerial)) throw std::runtime_error("Thumbnail atlas readback has not retired.");
    auto &Atlas = *Allocation;
    void *Mapped = nullptr;
    RequireAtlasSuccess(Atlas.MapMemory(Atlas.Access.Device, Atlas.ReadbackMemory, 0, VK_WHOLE_SIZE, 0, &Mapped));
    std::vector<uint8_t> Pixels(size_t(Atlas.Extent) * Atlas.Extent * 4);
    std::memcpy(Pixels.data(), Mapped, Pixels.size());
    Atlas.UnmapMemory(Atlas.Access.Device, Atlas.ReadbackMemory);
    Atlas.ReadbackRecorded = false;
    return Pixels;
}
void MaterialThumbnailAtlas::ConfirmSubmission() noexcept
{
    if (Allocation && Allocation->Recorded)
    {
        Allocation->Submitted = true;
        Allocation->Recorded = false;
    }
}
VkImage MaterialThumbnailAtlas::QueryImage() const noexcept { return Allocation ? Allocation->Image : VK_NULL_HANDLE; }
VkImageView MaterialThumbnailAtlas::QueryImageView() const noexcept { return Allocation ? Allocation->View : VK_NULL_HANDLE; }
uint32_t MaterialThumbnailAtlas::QueryExtent() const noexcept { return Allocation ? Allocation->Extent : 0; }
uint32_t MaterialThumbnailAtlas::QueryCapacity() const noexcept { return Allocation ? Allocation->Columns * Allocation->Columns : 0; }
ThumbnailUvRectangle MaterialThumbnailAtlas::QueryUv(uint32_t Slot) const
{
    if (!Allocation || Slot >= QueryCapacity()) throw std::runtime_error("Unknown thumbnail atlas slot.");
    const auto &Atlas = *Allocation;
    const float X = float((Slot % Atlas.Columns) * (Atlas.Tile + 2) + 1), Y = float((Slot / Atlas.Columns) * (Atlas.Tile + 2) + 1);
    return {X / Atlas.Extent, Y / Atlas.Extent, (X + Atlas.Tile) / Atlas.Extent, (Y + Atlas.Tile) / Atlas.Extent};
}
} // namespace Frontier
