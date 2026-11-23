/*============================================================================================================================================
                                                      VECTORSELECTIONEVALUATION.CPP
============================================================================================================================================*/
// 🧩 Evaluate continuous document coordinates on the GPU, then authenticate retired token readback without queue waits.

//------------------------------------------------------------------------------------------------------------------------
//                                                     INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "VectorSelectionEvaluation.h"
#include <algorithm>
#include <cstring>
#include <fstream>

namespace Frontier
{
namespace
{
void ConfirmSelectionResult(VkResult Result)
{
    if (Result != VK_SUCCESS) throw std::runtime_error("Vulkan vector selection failed: " + std::to_string(int(Result)));
}

struct SelectionRequest
{
    VectorSelectionProgram Program;
    std::array<float, 2> Pointer;
    bool operator==(const SelectionRequest &Other) const
    {
        return Pointer == Other.Pointer && Program.Candidates == Other.Program.Candidates &&
            Program.Coverage.Root == Other.Program.Coverage.Root &&
            Program.Coverage.Instructions == Other.Program.Coverage.Instructions &&
            Program.Coverage.Segments == Other.Program.Coverage.Segments;
    }
};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     RETIRED SELECTION ALLOCATION
//------------------------------------------------------------------------------------------------------------------------

struct VectorSelectionEvaluation::SelectionAllocation
{
    VulkanRasterAccess Access;
    VectorCoverageRasterization Rasterization;
    VkBuffer Readback = VK_NULL_HANDLE;
    VkDeviceMemory Storage = VK_NULL_HANDLE;
    uint64_t SubmittedSerial = 0, RecordedSerial = 0;
    uint32_t Token = 0;
    std::optional<SelectionRequest> Requested, Recorded, Submitted, Completed;
#define FRONTIER_SELECTION_FUNCTIONS(Function) \
    Function(CreateBuffer) Function(DestroyBuffer) Function(GetBufferMemoryRequirements) Function(AllocateMemory) \
    Function(FreeMemory) Function(BindBufferMemory) Function(MapMemory) Function(UnmapMemory) \
    Function(CmdPipelineBarrier) Function(CmdCopyImageToBuffer)
#define FRONTIER_SELECTION_POINTER(Name) PFN_vk##Name Name = nullptr;
    FRONTIER_SELECTION_FUNCTIONS(FRONTIER_SELECTION_POINTER)
#undef FRONTIER_SELECTION_POINTER
    ~SelectionAllocation()
    {
        Rasterization.Finalize();
        if (Readback) DestroyBuffer(Access.Device, Readback, nullptr);
        if (Storage) FreeMemory(Access.Device, Storage, nullptr);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     PUBLIC GPU SELECTION OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

VectorSelectionEvaluation::VectorSelectionEvaluation() = default;
VectorSelectionEvaluation::~VectorSelectionEvaluation() = default;

void VectorSelectionEvaluation::Initialize(const VulkanRasterAccess &Access, RasterShader Shader)
{
    if (Allocation || !Access.Device || !Access.ResolveDevice || !Access.QueryMemory)
        throw std::runtime_error("Invalid vector selection initialization.");
    auto Resident = std::make_unique<SelectionAllocation>();
    Resident->Access = Access;
#define FRONTIER_SELECTION_RESOLVE(Name) Resident->Name = reinterpret_cast<PFN_vk##Name>(Access.ResolveDevice(Access.Device, "vk" #Name)); if (!Resident->Name) throw std::runtime_error("Missing vector selection function: vk" #Name);
    FRONTIER_SELECTION_FUNCTIONS(FRONTIER_SELECTION_RESOLVE)
#undef FRONTIER_SELECTION_RESOLVE
    Resident->Rasterization.Initialize(Access, Shader, 1, 1);
    VkBufferCreateInfo Creation{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    Creation.size = 4; Creation.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    ConfirmSelectionResult(Resident->CreateBuffer(Access.Device, &Creation, nullptr, &Resident->Readback));
    VkMemoryRequirements Requirements{};
    Resident->GetBufferMemoryRequirements(Access.Device, Resident->Readback, &Requirements);
    VkPhysicalDeviceMemoryProperties Memory{};
    Access.QueryMemory(Access.PhysicalDevice, &Memory);
    uint32_t MemoryIndex = UINT32_MAX;
    constexpr auto Properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    for (uint32_t Index = 0; Index < Memory.memoryTypeCount; ++Index)
        if ((Requirements.memoryTypeBits & (1u << Index)) && (Memory.memoryTypes[Index].propertyFlags & Properties) == Properties)
        {
            MemoryIndex = Index; break;
        }
    if (MemoryIndex == UINT32_MAX) throw std::runtime_error("No coherent vector selection readback memory.");
    VkMemoryAllocateInfo AllocationDescription{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    AllocationDescription.allocationSize = Requirements.size;
    AllocationDescription.memoryTypeIndex = MemoryIndex;
    ConfirmSelectionResult(Resident->AllocateMemory(Access.Device, &AllocationDescription, nullptr, &Resident->Storage));
    ConfirmSelectionResult(Resident->BindBufferMemory(Access.Device, Resident->Readback, Resident->Storage, 0));
    Allocation = std::move(Resident);
}

void VectorSelectionEvaluation::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive)
{
    std::ifstream Input(ShaderArchive, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4) throw std::runtime_error("Missing vector selection shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes)) throw std::runtime_error("Unreadable vector selection shader archive.");
    Initialize(Access, RasterShader{Words, "main"});
}

std::optional<uint32_t> VectorSelectionEvaluation::QueryToken(const VectorSelectionProgram &Program, std::array<float, 2> Pointer)
{
    if (!Allocation) throw std::runtime_error("GPU vector selection is unavailable.");
    ValidateVectorSelection(Program);
    if (!std::isfinite(Pointer[0]) || !std::isfinite(Pointer[1]) || std::abs(Pointer[0]) > 1e6f || std::abs(Pointer[1]) > 1e6f)
        throw std::runtime_error("Vector selection pointer exceeds the finite document domain.");
    SelectionRequest Request{Program, Pointer};
    auto &Resident = *Allocation;
    Resident.Requested = Request;
    if (Resident.Completed && *Resident.Completed == Request) return Resident.Token;
    return std::nullopt;
}

void VectorSelectionEvaluation::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    if (!Allocation) return;
    auto &Resident = *Allocation;
    if (Resident.Recorded) throw std::runtime_error("Confirm selection submission before recording again.");
    if (Resident.Submitted)
    {
        if (Resident.SubmittedSerial > CompletedSerial) return;
        void *Mapped = nullptr;
        ConfirmSelectionResult(Resident.MapMemory(Resident.Access.Device, Resident.Storage, 0, 4, 0, &Mapped));
        const auto *Bytes = static_cast<const unsigned char *>(Mapped);
        const uint32_t Token = uint32_t(Bytes[0]) | (uint32_t(Bytes[1]) << 8) | (uint32_t(Bytes[2]) << 16) | (uint32_t(Bytes[3]) << 24);
        Resident.UnmapMemory(Resident.Access.Device, Resident.Storage);
        if (Token && std::none_of(Resident.Submitted->Program.Candidates.begin(), Resident.Submitted->Program.Candidates.end(),
                [Token](const auto &Candidate) { return Candidate[1] == Token; }))
            throw std::runtime_error("GPU selection returned an unauthenticated source token.");
        Resident.Token = Token;
        Resident.Completed = std::move(Resident.Submitted);
        Resident.Submitted.reset();
    }
    if (!Resident.Requested || (Resident.Completed && *Resident.Completed == *Resident.Requested)) return;
    Resident.Rasterization.ConfigureRetiredSelection(Resident.Requested->Program, Resident.Requested->Pointer, CompletedSerial);
    (void)Resident.Rasterization.ConstructCommands(Commands, CompletedSerial, SubmissionSerial);
    VkImageMemoryBarrier Image{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    Image.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; Image.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    Image.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; Image.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    Image.srcQueueFamilyIndex = Image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    Image.image = Resident.Rasterization.QueryImage();
    Image.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Resident.CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &Image);
    VkBufferImageCopy Region{};
    Region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; Region.imageExtent = {1, 1, 1};
    Resident.CmdCopyImageToBuffer(Commands, Image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Resident.Readback, 1, &Region);
    Image.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; Image.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Image.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; Image.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkMemoryBarrier Host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    Host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; Host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    Resident.CmdPipelineBarrier(Commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &Host, 0, nullptr, 1, &Image);
    Resident.Recorded = Resident.Requested;
    Resident.RecordedSerial = SubmissionSerial;
}

void VectorSelectionEvaluation::ConfirmSubmission()
{
    if (!Allocation || !Allocation->Recorded) return;
    auto &Resident = *Allocation;
    Resident.Rasterization.ConfirmSubmission();
    Resident.Submitted = std::move(Resident.Recorded); Resident.Recorded.reset();
    Resident.SubmittedSerial = Resident.RecordedSerial;
}

void VectorSelectionEvaluation::Finalize() noexcept { Allocation.reset(); }
uint64_t VectorSelectionEvaluation::QueryDispatchCount() const noexcept
{
    return Allocation ? Allocation->Rasterization.QueryDispatchCount() : 0;
}
}
