/*============================================================================================================================================
                                                        VECTORAUTHORINGCOVERAGERENDERING.CPP
============================================================================================================================================*/
// 🧩 Track cached sampling serials and bounded retired output slots; authoring descriptors remain independent of GPU resource lifetime.

#include "VectorAuthoringCoverageRendering.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <algorithm>
#include <fstream>

namespace Frontier
{
namespace
{
bool EquivalentProgram(const VectorCoverageProgram &First, const VectorCoverageProgram &Second)
{
    return First.Root == Second.Root && First.Instructions == Second.Instructions && First.Segments == Second.Segments;
}
constexpr std::array<uint32_t, 6> PooledExtents{128, 256, 512, 1024, 2048, 4096};
size_t IndexForExtent(uint32_t Extent)
{
    for (size_t Index = 0; Index < PooledExtents.size(); ++Index)
        if (PooledExtents[Index] == Extent) return Index;
    return Extent <= 128 ? 0 : Extent <= 256 ? 1 : Extent <= 512 ? 2 : Extent <= 1024 ? 3 : Extent <= 2048 ? 4 : 5;
}
}

void VectorAuthoringCoverageRendering::Initialize(const VulkanRasterAccess &Access, RasterShader Shader)
{
    RetainedAccess = Access;
    RetainedShaderWords.assign(Shader.Words.begin(), Shader.Words.end());
    RetainedShaderEntry = Shader.Entry ? Shader.Entry : "";
}

size_t VectorAuthoringCoverageRendering::SelectPooledTier(size_t ActiveTier, size_t Segments, uint32_t DisplayExtent) noexcept
{
    const size_t Which = IndexForExtent(VectorCoverageRasterization::SelectBakeExtent(Segments, 1, DisplayExtent));
    if (!DisplayExtent || ActiveTier >= PooledExtents.size() || Which == ActiveTier) return Which;
    // 📝 Two-sided hysteresis (a Schmitt trigger) against a jittering device-pixel hint near a pooled-tile
    // boundary (layout rounding, DPI scale, a sub-pixel zoom): once settled on a tier, only leave it once the
    // request clears a tolerance band around that tier's own extent, in EITHER direction. The original guard
    // only dampened a one-tile *upgrade* with a tiny ActiveExtent/32 tolerance (4px at the smallest 128
    // tile) — a jitter source producing anything larger than that (ordinary layout/DPI rounding routinely
    // does, e.g. ~16px) still flips the tier every single frame: upgrade dampened one frame, downgrade (never
    // dampened at all) fires the next, forever. That perpetual re-dispatch is why RefitSlot never settles and
    // QuerySelectedReady() never reports ready — the "Baking..." badge got stuck even on a perfectly idle,
    // empty canvas. Dampening the downgrade symmetrically, with a much wider tolerance, closes that loop. A
    // real zoom change still moves the request by a wide margin (not just past the tile boundary by a few
    // device pixels) and is never swallowed by either side of this band.
    const uint32_t ActiveExtent = PooledExtents[ActiveTier];
    const uint32_t Affordable = VectorCoverageRasterization::SelectBakeExtent(Segments, 1, 0);
    const uint32_t Tolerance = std::max<uint32_t>(32, ActiveExtent / 8);
    if (Which == ActiveTier + 1 && ActiveExtent <= Affordable && DisplayExtent <= ActiveExtent + Tolerance) return ActiveTier;
    if (ActiveTier > 0 && Which == ActiveTier - 1 && DisplayExtent + Tolerance >= PooledExtents[ActiveTier - 1]) return ActiveTier;
    return Which;
}

bool VectorAuthoringCoverageRendering::RefitSlot(size_t Index, size_t Segments, uint32_t DisplayExtent)
{
    // 📝 The canvas runs the same unguarded curve loop as the bridge, single
    // channel; retired slots re-bracket before dense documents dispatch.
    auto &Slot = Slots[Index];
    const size_t Which = SelectPooledTier(Slot.Active, Segments, DisplayExtent);
    if (Which == Slot.Active) return false;
    const uint32_t Needed = PooledExtents[Which];
    if (!Slot.Extents[Which])
    {
        auto Fresh = std::make_unique<VectorCoverageRasterization>();
        Fresh->Initialize(RetainedAccess, RasterShader{RetainedShaderWords, RetainedShaderEntry.c_str()}, Needed, Needed);
        Slot.Extents[Which] = std::move(Fresh);
        DiagnosticJournal::Append("PatternBake", "canvas slot " + std::to_string(Index) + " pooled extent " +
            std::to_string(Needed) + " for " + std::to_string(Segments) + " segments");
    }
    Slot.Active = Which;
    return true;
}

VectorCoverageRasterization &VectorAuthoringCoverageRendering::ActiveRasterization(size_t Index)
{
    auto *Active = Slots.at(Index).Extents.at(Slots.at(Index).Active).get();
    if (!Active) throw std::runtime_error("Authoring coverage slot has no fitted rasterization.");
    return *Active;
}

const VectorCoverageRasterization *VectorAuthoringCoverageRendering::QueryActiveRasterization(size_t Slot) const
{
    const auto &Entry = Slots.at(Slot);
    return Entry.Active < Entry.Extents.size() ? Entry.Extents[Entry.Active].get() : nullptr;
}

void VectorAuthoringCoverageRendering::Initialize(const VulkanRasterAccess &Access, const std::filesystem::path &ShaderArchive)
{
    std::ifstream Input(ShaderArchive, std::ios::binary | std::ios::ate);
    const auto Bytes = Input.tellg();
    if (!Input || Bytes < 20 || Bytes > 16777216 || Bytes % 4) throw std::runtime_error("Missing or malformed vector shader archive.");
    std::vector<uint32_t> Words(size_t(Bytes) / 4);
    Input.seekg(0);
    if (!Input.read(reinterpret_cast<char *>(Words.data()), Bytes) || Words[0] != 0x07230203) throw std::runtime_error("Invalid vector SPIR-V archive.");
    Initialize(Access, RasterShader{Words, "main"});
}

bool VectorAuthoringCoverageRendering::RequiresUnchangedResubmission(bool Retiled, uint64_t SampledSerial) noexcept
{
    // 📝 A real re-tile (the pooled tier actually changed) always needs a fresh dispatch. Otherwise,
    // only the slot's very first-ever bake (it has never completed a submission) still needs one; once
    // settled, unchanged content stays valid on the GPU without being redrawn every frame.
    return Retiled || SampledSerial == 0;
}

int VectorAuthoringCoverageRendering::SelectProgram(const VectorCoverageProgram &Program, uint32_t DisplayExtent)
{
    ValidateVectorCoverage(Program);
    if (Selected >= 0 && Slots[size_t(Selected)].Program && EquivalentProgram(*Slots[size_t(Selected)].Program, Program))
    {
        // 📝 Unchanged content: only a DisplayExtent hint (e.g. the canvas zoomed) can still ask for a
        // different pooled tier. Re-fitting re-dispatches the same program into the new tier's image.
        // Skip the attempt while the slot's current image is still sampled by an earlier submission
        // (mirrors the never-block-the-device rule below); it retries on a later, settled frame.
        auto &Slot = Slots[size_t(Selected)];
        bool Retiled = false;
        if (Slot.SampledSerial <= Completed) Retiled = RefitSlot(size_t(Selected), Program.Segments.size(), DisplayExtent);
        if (Retiled) Slot.Pending = true;
        SampleRequested = RequiresUnchangedResubmission(Retiled, Slot.SampledSerial);
        return Selected;
    }
    for (size_t Index = 0; Index < Slots.size(); ++Index)
    {
        auto &Slot = Slots[Index];
        if (Slot.SampledSerial > Completed) continue;
        RefitSlot(Index, Program.Segments.size(), DisplayExtent);
        Slot.Program = Program;
        Slot.SurfaceProgram.reset();
        Slot.Pending = true;
        Selected = int(Index);
        SampleRequested = true;
        return Selected;
    }
    // 📝 Keep the last valid image visible until a different slot retires; never block the device for an edit.
    SampleRequested = Selected >= 0;
    return Selected;
}

int VectorAuthoringCoverageRendering::SelectSurfaceProgram(const VectorSurfaceProgram &Program, uint32_t DisplayExtent)
{
    ValidateVectorSurface(Program);
    if (Selected >= 0 && Slots[size_t(Selected)].SurfaceProgram && *Slots[size_t(Selected)].SurfaceProgram == Program)
    {
        auto &Slot = Slots[size_t(Selected)];
        bool Retiled = false;
        if (Slot.SampledSerial <= Completed) Retiled = RefitSlot(size_t(Selected), Program.Coverage.Segments.size(), DisplayExtent);
        if (Retiled) Slot.Pending = true;
        SampleRequested = RequiresUnchangedResubmission(Retiled, Slot.SampledSerial);
        return Selected;
    }
    for (size_t Index = 0; Index < Slots.size(); ++Index)
    {
        auto &Slot = Slots[Index];
        if (Slot.SampledSerial > Completed) continue;
        RefitSlot(Index, Program.Coverage.Segments.size(), DisplayExtent);
        Slot.SurfaceProgram = Program;
        Slot.Program.reset();
        Slot.Pending = true;
        Selected = int(Index);
        SampleRequested = true;
        return Selected;
    }
    SampleRequested = Selected >= 0;
    return Selected;
}

void VectorAuthoringCoverageRendering::ConstructCommands(VkCommandBuffer Commands, uint64_t CompletedSerial, uint64_t SubmissionSerial)
{
    Completed = std::max(Completed, CompletedSerial);
    PendingSerial = 0;
    if (!SampleRequested || Selected < 0) return;
    auto &Slot = Slots[size_t(Selected)];
    if (Slot.Pending)
    {
        if (Slot.SampledSerial > Completed) throw std::runtime_error("Authoring coverage output is still sampled by an earlier submission.");
        if (Slot.SurfaceProgram) ActiveRasterization(size_t(Selected)).ConfigureRetiredSurface(*Slot.SurfaceProgram, Completed);
        else ActiveRasterization(size_t(Selected)).ConfigureRetiredProgram(*Slot.Program, Completed);
        Slot.Pending = false;
    }
    (void)ActiveRasterization(size_t(Selected)).ConstructCommands(Commands, Completed, SubmissionSerial);
    PendingSerial = SubmissionSerial;
}

void VectorAuthoringCoverageRendering::ConfirmSubmission()
{
    if (PendingSerial && Selected >= 0)
    {
        auto &Slot = Slots[size_t(Selected)];
        ActiveRasterization(size_t(Selected)).ConfirmSubmission(PendingSerial);
        Slot.SampledSerial = PendingSerial;
    }
    PendingSerial = 0;
    SampleRequested = false;
}

VkImage VectorAuthoringCoverageRendering::QueryImage(size_t Slot) const
{
    const auto *Active = QueryActiveRasterization(Slot);
    return Active ? Active->QueryImage() : VK_NULL_HANDLE;
}

VkImageView VectorAuthoringCoverageRendering::QueryImageView(size_t Slot) const
{
    const auto *Active = QueryActiveRasterization(Slot);
    return Active ? Active->QueryImageView() : VK_NULL_HANDLE;
}

uint32_t VectorAuthoringCoverageRendering::QuerySlotExtent(size_t Slot) const
{
    const auto &Entry = Slots.at(Slot);
    return Entry.Active < PooledExtents.size() ? PooledExtents[Entry.Active] : 0;
}

bool VectorAuthoringCoverageRendering::QuerySelectedReady() const noexcept
{
    if (Selected < 0) return false;
    const auto &Slot = Slots[size_t(Selected)];
    return !Slot.Pending && Slot.SampledSerial != 0 && Slot.SampledSerial <= Completed;
}

uint64_t VectorAuthoringCoverageRendering::QueryDispatchCount() const noexcept
{
    uint64_t Count = 0;
    for (const auto &Slot : Slots)
        for (const auto &Extent : Slot.Extents)
            if (Extent) Count += Extent->QueryDispatchCount();
    return Count;
}

void VectorAuthoringCoverageRendering::Finalize() noexcept
{
    for (auto &Slot : Slots)
    {
        for (auto &Extent : Slot.Extents)
            if (Extent) Extent->Finalize();
        Slot.Extents = {};
        Slot.Active = PooledExtents.size();
        Slot.Program.reset();
        Slot.SurfaceProgram.reset();
        Slot.SampledSerial = 0;
        Slot.Pending = false;
    }
    Completed = PendingSerial = 0;
    Selected = -1;
    SampleRequested = false;
}
}
