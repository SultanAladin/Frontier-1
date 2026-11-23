/*==========================================================================================================================================
                                                    PATHREPLAYDOMAIN.H
==========================================================================================================================================*/
// 🧩 GPU-compatible replay inputs, guarded transport domain and emissive-facet index compilation.
#pragma once

#include "PathReplayFootprint.h"
#include "TriangleVisibilitySpace.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  BOUNDED SCENE VIEW
//------------------------------------------------------------------------------------------------------------------------

// 📝 Non-owning view: all spans must remain alive and immutable while validation or compilation is running.
struct PathReplaySceneView
{
    std::span<const PathReplayReceiver> Receivers;
    std::span<const PathReplayFacet> Surfaces;
    std::span<const TriangleVisibilitySpace::FloatEnclosure> Enclosures;
    std::span<const TriangleVisibilitySpace::FloatFacet> Corners;
    std::span<const float> MaterialAtlas;
    std::span<const std::array<float, 4>> Camera;
    std::size_t AnalyticLightCount = 0;
};

struct PathReplayDomainSpecification
{
    bool PrimaryVisibility = false;
    bool RoughOpaque = false;
    bool TexturedDiffuse = false;
    bool ImportanceRough = false;
    bool DiffuseCameraReprojection = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  BOUNDED DOMAIN INTAKE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Throws std::runtime_error on inputs outside the verified opaque triangle, texture and BVH domain.
//     This is not a production renderer submission or a claim of complete ReSTIR GI.
void ValidatePathReplayDomain(const PathReplaySceneView& Scene, const PathReplayDomainSpecification& Specification);

// 📝 Call after domain validation; returns exact float32 indices for nondegenerate emissive facets in input order.
std::vector<float> CompilePathReplayFacetSources(std::span<const PathReplayFacet> Surfaces);

//------------------------------------------------------------------------------------------------------------------------
//                                                  REPLAY HISTORY INTAKE
//------------------------------------------------------------------------------------------------------------------------

struct PathReplayHistorySpecification
{
    uint32_t BounceCount = 0;
    float PositiveFloor = 0;
    std::array<float, 3> Environment{};
    bool PrimaryVisibility = false;
    bool RoughOpaque = false;
    bool SampleFacets = false;
    bool DoubleCountForAudit = false;
    bool StrictMotionForAudit = false;
    bool BalanceSampling = false;
    bool TexturedDiffuse = false;
    bool ImportanceRough = false;
    bool PowerWeightedFacets = false;
    bool UniformFacetMixture = false;
    uint32_t InitialCandidates = 1;
    bool operator==(const PathReplayHistorySpecification&) const = default;
};

struct PathReplayHistoryDecision
{
    bool Changed = true;
    uint32_t MaximumHistory = 1;
    bool CameraOnly = false;
};

// 📝 Tracks exact input revisions, including in-place atlas and BVH changes. CameraOnly additionally requires
//     the supplied previous camera to match the last resolved camera and unchanged non-camera metadata.
//     This is a bounded heuristic, not a proof of unbiased adaptive reuse; it does not own GPU histories,
//     validate submissions or issue renderer commands.
class PathReplayHistoryDomain
{
  public:
    [[nodiscard]] PathReplayHistoryDecision Resolve(const PathReplaySceneView& Scene,
                                                     const PathReplayHistorySpecification& Specification,
                                                     bool FullHistoryForAudit = false);
    void Invalidate() noexcept;

  private:
    bool PriorAvailable = false;
    PathReplayHistorySpecification PriorSpecification{};
    std::array<std::array<float, 4>, 12> PriorCamera{};
    std::vector<PathReplayFacet> PriorSurfaces;
    std::vector<PathReplayReceiver> PriorReceivers;
    std::vector<TriangleVisibilitySpace::FloatEnclosure> PriorEnclosures;
    std::vector<TriangleVisibilitySpace::FloatFacet> PriorCorners;
    std::vector<float> PriorMaterialAtlas;
};

} // namespace Frontier
