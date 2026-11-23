/*============================================================================================================================================
                                                     VECTORSURFACEMATERIALBRIDGE.H
============================================================================================================================================*/
// 🧩 Stage six retired Float32 vector channels into coherent PBR material-map pyramids.

#pragma once
#include "Engine/Render/SurfaceMaterial.h"
#include "Engine/Render/VectorCoverageRasterization.h"
#include "Engine/Render/VectorSurfaceCompilation.h"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{
class VectorSurfaceMaterialBridge
{
public:
    VectorSurfaceMaterialBridge();
    ~VectorSurfaceMaterialBridge();
    VectorSurfaceMaterialBridge(const VectorSurfaceMaterialBridge &) = delete;
    VectorSurfaceMaterialBridge &operator=(const VectorSurfaceMaterialBridge &) = delete;
    void Initialize(const VulkanRasterAccess &Access, RasterShader Shader, uint32_t Width, uint32_t Height);
    void Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive, uint32_t Width, uint32_t Height);
    bool ConfigureDocument(const VectorAuthoringDocument &Document,uint64_t CompletedSerial);
    bool ConstructCommands(VkCommandBuffer Commands,uint64_t CompletedSerial,uint64_t SubmissionSerial);
    void ConfirmSubmission() noexcept;
    [[nodiscard]] std::array<MaterialTexturePyramid,6> RetrieveAuthoringMaps(uint64_t CompletedSerial);
    void Finalize() noexcept;
    // 📝 Reduced-bucket readbacks are bilinearly restored to full material
    // resolution so downstream pyramids keep their exact shape; the normal
    // channel decodes, renormalizes and re-encodes like the mip builder.
    [[nodiscard]] static std::vector<float> UpsampleFloatRgba(std::span<const float> Source, uint32_t Width,
        uint32_t Height, uint32_t TargetExtent, bool RenormalizeNormals);
private:
    std::array<VectorCoverageRasterization,6> Rasterizations;
    std::array<VectorSurfaceProgram,6> Programs;
    std::optional<std::array<MaterialTexturePyramid,6>> CompiledMaps;
    VulkanRasterAccess RetainedAccess{};
    std::vector<uint32_t> RetainedShaderWords;
    std::string RetainedShaderEntry;
    uint32_t BakeExtent = 0;
    uint64_t LastSubmission = 0,PendingSubmission = 0;
    bool Initialized = false,ProgramsConfigured = false,Dirty = false,Recorded = false,ReadbacksPending = false;
};
}
