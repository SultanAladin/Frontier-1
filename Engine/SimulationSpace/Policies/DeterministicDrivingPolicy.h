/*============================================================================================================================================
                                                      DETERMINISTICDRIVINGPOLICY.H
============================================================================================================================================*/
// 🧩 Deterministic 16->20->3 (403-float / 1,612 B) Tanh driving policy, 4-byte quantized directive codec, and CEM-RL self-play trainer

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Container/DocumentContainerCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace Frontier::SimulationSpace::Policies
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct DrivingObservationVector
{
    static constexpr std::size_t InputCount = 16u; // [count] - 9-ray rangefan + 7 pursuit kinematic features

    std::array<float, InputCount> Features{}; // [-] - Normalized sensor & relative pursuit inputs
};

struct DrivingControlOutput
{
    float Steer    = 0.0f; // [-] - Normalized steering command [-1..+1]
    float Throttle = 0.0f; // [-] - Normalized throttle command [0..1]
    float Brake    = 0.0f; // [-] - Normalized brake command [0..1]
};

struct QuantizedDrivingControl
{
    int16_t SteerQ15   = 0;  // [-] - Signed 16-bit fixed-point steer [-32767..+32767]
    uint8_t ThrottleQ8 = 0u; // [-] - Unsigned 8-bit throttle [0..255]
    uint8_t BrakeQ8    = 0u; // [-] - Unsigned 8-bit brake [0..255]
};

static_assert(sizeof(QuantizedDrivingControl) == 4u, "QuantizedDrivingControl must be exactly 4 bytes for low-bandwidth replication.");

struct CrossEntropyTrainingConfig
{
    uint32_t PopulationSize = 24u;     // [count] - Candidate policies evaluated per epoch
    uint32_t EliteCount     = 6u;      // [count] - Top-scoring candidates folded into mean/std update
    float    InitialSigma   = 0.25f;   // [-]     - Parameter perturbation standard deviation
    float    SmoothingAlpha = 0.70f;   // [-]     - Exponential blend toward elite distribution
    uint64_t EpochSeed      = 0xF007ull;// [-]    - Deterministic PRNG seed for candidate sampling
};

struct CrossEntropyEpochMetrics
{
    float    BestEpisodeScore = 0.0f; // [pts]   - Highest candidate reward in epoch
    float    EliteMeanScore   = 0.0f; // [pts]   - Mean reward across top-K elites
    float    UpdatedSigma     = 0.0f; // [-]     - Post-epoch exploration standard deviation
    uint32_t CandidatesTested = 0u;   // [count] - Total rollouts completed in epoch
};

class DeterministicDrivingPolicy;
using DrivingEpisodeEvaluator = float (*)(const DeterministicDrivingPolicy& Candidate, void* UserPayload);

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class DeterministicDrivingPolicy
{
public:
    static constexpr std::size_t InputDimensions  = 16u;  // [count] - Observation input vector width
    static constexpr std::size_t HiddenDimensions = 20u;  // [count] - Hidden Tanh layer width
    static constexpr std::size_t OutputDimensions = 3u;   // [count] - [Steer, Throttle, Brake] output width
    static constexpr std::size_t ParameterCount   =
        (InputDimensions * HiddenDimensions) + HiddenDimensions +
        (HiddenDimensions * OutputDimensions) + OutputDimensions; // [count] - 320 + 20 + 60 + 3 = 403 floats
    static constexpr std::size_t WeightSlabBytes  = ParameterCount * sizeof(float); // [B] - 1,612 bytes

    DeterministicDrivingPolicy() noexcept = default;

    void SeedInitialize(uint64_t DeterministicSeed) noexcept;

    [[nodiscard]] DrivingControlOutput EvaluateDrivingControl(const DrivingObservationVector& Observation) const noexcept;

    [[nodiscard]] static QuantizedDrivingControl QuantizeControl(const DrivingControlOutput& Control) noexcept;
    [[nodiscard]] static DrivingControlOutput    DequantizeControl(const QuantizedDrivingControl& Quantized) noexcept;

    [[nodiscard]] uint64_t ComputeParameterDigest() const noexcept;

    [[nodiscard]] bool EncodeWeightBlob(std::array<std::byte, WeightSlabBytes>& OutBytes) const noexcept;
    [[nodiscard]] bool DecodeWeightBlob(std::span<const std::byte> InBytes, std::string& OutDiagnostic);

    [[nodiscard]] bool EncodeToDocument(
        std::string_view                  PolicyTitle,
        Serialization::DocumentContainer& OutDocument) const;

    [[nodiscard]] bool DecodeFromDocument(
        const Serialization::DocumentContainer& Document,
        std::string&                            OutDiagnostic);

    [[nodiscard]] bool RunCrossEntropyEpoch(
        const CrossEntropyTrainingConfig& Config,
        DrivingEpisodeEvaluator           Evaluator,
        void*                             EvaluatorPayload,
        CrossEntropyEpochMetrics&         OutMetrics);

    [[nodiscard]] const std::array<float, ParameterCount>& InspectParameters() const noexcept { return Parameters; }
    [[nodiscard]] std::array<float, ParameterCount>&       AccessParameters() noexcept { return Parameters; }
    [[nodiscard]] uint64_t                                 QueryOriginSeed() const noexcept { return OriginSeed; }

private:
    std::array<float, ParameterCount> Parameters{};       // [-] - Contiguous 403-float weight slab [W1, b1, W2, b2]
    uint64_t                          OriginSeed = 1ull;  // [-] - Seed used to initialize or last train the policy
};

static_assert(DeterministicDrivingPolicy::ParameterCount == 403u, "16->20->3 MLP must contain exactly 403 parameters.");
static_assert(DeterministicDrivingPolicy::WeightSlabBytes == 1612u, "403 float32 parameters must occupy exactly 1,612 bytes.");

} // namespace Frontier::SimulationSpace::Policies
