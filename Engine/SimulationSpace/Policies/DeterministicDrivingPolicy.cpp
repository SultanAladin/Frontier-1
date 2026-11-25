/*============================================================================================================================================
                                                     DETERMINISTICDRIVINGPOLICY.CPP
============================================================================================================================================*/
// 🧩 Implementation of 16->20->3 (403-float / 1,612 B) Tanh driving policy, 4B quantized directive codec, and CEM-RL trainer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Policies/DeterministicDrivingPolicy.h"

#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <sstream>
#include <vector>

namespace Frontier::SimulationSpace::Policies
{

namespace
{

// Deterministic SplitMix64 generator guaranteeing identical weights & rollouts across compilers and OS targets
struct DeterministicSplitMix64
{
    uint64_t Cursor = 0x9E3779B97F4A7C15ull;

    explicit DeterministicSplitMix64(uint64_t Seed) noexcept
        : Cursor(Seed != 0ull ? Seed : 0x9E3779B97F4A7C15ull)
    {
    }

    [[nodiscard]] uint64_t NextUint64() noexcept
    {
        uint64_t Z = (Cursor += 0x9E3779B97F4A7C15ull);
        Z          = (Z ^ (Z >> 30u)) * 0xBF58476D1CE4E5B9ull;
        Z          = (Z ^ (Z >> 27u)) * 0x94D049BB133111EBull;
        return Z ^ (Z >> 31u);
    }

    [[nodiscard]] float NextUniformUnit() noexcept
    {
        // Map top 24 bits cleanly to [0, 1)
        const uint32_t Mantissa = static_cast<uint32_t>(NextUint64() >> 40u);
        return static_cast<float>(Mantissa) * (1.0f / 16777216.0f);
    }

    [[nodiscard]] float NextSymmetricUnit() noexcept
    {
        return (NextUniformUnit() * 2.0f) - 1.0f;
    }

    [[nodiscard]] float NextApproximateGaussian() noexcept
    {
        // Irwin-Hall sum of 6 uniforms centered at 0 with variance = 0.5 -> scaled to unit std
        float Sum = 0.0f;
        for (uint32_t Sample = 0u; Sample < 6u; ++Sample)
        {
            Sum += NextUniformUnit();
        }
        return (Sum - 3.0f) * 1.41421356f;
    }
};

void WriteLe32Float(std::byte* Dest, float Value) noexcept
{
    uint32_t Bits = 0u;
    std::memcpy(&Bits, &Value, sizeof(uint32_t));
    Dest[0] = static_cast<std::byte>(Bits & 0xFFu);
    Dest[1] = static_cast<std::byte>((Bits >> 8u) & 0xFFu);
    Dest[2] = static_cast<std::byte>((Bits >> 16u) & 0xFFu);
    Dest[3] = static_cast<std::byte>((Bits >> 24u) & 0xFFu);
}

[[nodiscard]] float ReadLe32Float(const std::byte* Src) noexcept
{
    const uint32_t Bits = static_cast<uint32_t>(Src[0]) |
                          (static_cast<uint32_t>(Src[1]) << 8u) |
                          (static_cast<uint32_t>(Src[2]) << 16u) |
                          (static_cast<uint32_t>(Src[3]) << 24u);
    float Value = 0.0f;
    std::memcpy(&Value, &Bits, sizeof(float));
    return Value;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           DETERMINISTIC DRIVING POLICY
//------------------------------------------------------------------------------------------------------------------------

void DeterministicDrivingPolicy::SeedInitialize(uint64_t DeterministicSeed) noexcept
{
    OriginSeed = DeterministicSeed;
    DeterministicSplitMix64 Generator(DeterministicSeed);

    // Xavier-scaled initialization for 16->20 and 20->3 Tanh layers
    constexpr float ScaleLayer1 = 0.40824829f; // sqrt(6 / (16 + 20))
    constexpr float ScaleLayer2 = 0.51075392f; // sqrt(6 / (20 + 3))

    std::size_t Offset = 0u;
    for (std::size_t Idx = 0u; Idx < InputDimensions * HiddenDimensions; ++Idx)
    {
        Parameters[Offset++] = Generator.NextSymmetricUnit() * ScaleLayer1;
    }
    for (std::size_t Idx = 0u; Idx < HiddenDimensions; ++Idx)
    {
        Parameters[Offset++] = 0.0f;
    }
    for (std::size_t Idx = 0u; Idx < HiddenDimensions * OutputDimensions; ++Idx)
    {
        Parameters[Offset++] = Generator.NextSymmetricUnit() * ScaleLayer2;
    }
    for (std::size_t Idx = 0u; Idx < OutputDimensions; ++Idx)
    {
        Parameters[Offset++] = 0.0f;
    }
}

DrivingControlOutput DeterministicDrivingPolicy::EvaluateDrivingControl(
    const DrivingObservationVector& Observation) const noexcept
{
    constexpr std::size_t OffsetW1 = 0u;
    constexpr std::size_t OffsetB1 = OffsetW1 + (InputDimensions * HiddenDimensions);
    constexpr std::size_t OffsetW2 = OffsetB1 + HiddenDimensions;
    constexpr std::size_t OffsetB2 = OffsetW2 + (HiddenDimensions * OutputDimensions);

    std::array<float, HiddenDimensions> HiddenActivations{};
    for (std::size_t H = 0u; H < HiddenDimensions; ++H)
    {
        float Accumulator = Parameters[OffsetB1 + H];
        const std::size_t RowBase = OffsetW1 + (H * InputDimensions);
        for (std::size_t I = 0u; I < InputDimensions; ++I)
        {
            const float SafeInput = std::isfinite(Observation.Features[I])
                                        ? std::clamp(Observation.Features[I], -10.0f, 10.0f)
                                        : 0.0f;
            Accumulator += Parameters[RowBase + I] * SafeInput;
        }
        HiddenActivations[H] = std::tanh(Accumulator);
    }

    std::array<float, OutputDimensions> RawOut{};
    for (std::size_t O = 0u; O < OutputDimensions; ++O)
    {
        float Accumulator = Parameters[OffsetB2 + O];
        const std::size_t RowBase = OffsetW2 + (O * HiddenDimensions);
        for (std::size_t H = 0u; H < HiddenDimensions; ++H)
        {
            Accumulator += Parameters[RowBase + H] * HiddenActivations[H];
        }
        RawOut[O] = std::tanh(Accumulator);
    }

    DrivingControlOutput Control{};
    Control.Steer    = std::clamp(RawOut[0], -1.0f, 1.0f);
    Control.Throttle = std::clamp(0.5f * (RawOut[1] + 1.0f), 0.0f, 1.0f);
    Control.Brake    = std::clamp(0.5f * (RawOut[2] + 1.0f), 0.0f, 1.0f);
    return Control;
}

QuantizedDrivingControl DeterministicDrivingPolicy::QuantizeControl(const DrivingControlOutput& Control) noexcept
{
    const float SafeSteer    = std::isfinite(Control.Steer) ? std::clamp(Control.Steer, -1.0f, 1.0f) : 0.0f;
    const float SafeThrottle = std::isfinite(Control.Throttle) ? std::clamp(Control.Throttle, 0.0f, 1.0f) : 0.0f;
    const float SafeBrake    = std::isfinite(Control.Brake) ? std::clamp(Control.Brake, 0.0f, 1.0f) : 0.0f;

    QuantizedDrivingControl Quantized{};
    Quantized.SteerQ15   = static_cast<int16_t>(std::lround(static_cast<double>(SafeSteer) * 32767.0));
    Quantized.ThrottleQ8 = static_cast<uint8_t>(std::lround(static_cast<double>(SafeThrottle) * 255.0));
    Quantized.BrakeQ8    = static_cast<uint8_t>(std::lround(static_cast<double>(SafeBrake) * 255.0));
    return Quantized;
}

DrivingControlOutput DeterministicDrivingPolicy::DequantizeControl(const QuantizedDrivingControl& Quantized) noexcept
{
    DrivingControlOutput Control{};
    Control.Steer    = std::clamp(static_cast<float>(Quantized.SteerQ15) / 32767.0f, -1.0f, 1.0f);
    Control.Throttle = static_cast<float>(Quantized.ThrottleQ8) / 255.0f;
    Control.Brake    = static_cast<float>(Quantized.BrakeQ8) / 255.0f;
    return Control;
}

uint64_t DeterministicDrivingPolicy::ComputeParameterDigest() const noexcept
{
    std::array<std::byte, WeightSlabBytes> Wire{};
    (void)EncodeWeightBlob(Wire);
    return Serialization::ComputeFnv1a64(Wire);
}

bool DeterministicDrivingPolicy::EncodeWeightBlob(std::array<std::byte, WeightSlabBytes>& OutBytes) const noexcept
{
    for (std::size_t Idx = 0u; Idx < ParameterCount; ++Idx)
    {
        const float SafeParam = std::isfinite(Parameters[Idx]) ? Parameters[Idx] : 0.0f;
        WriteLe32Float(OutBytes.data() + (Idx * sizeof(float)), SafeParam);
    }
    return true;
}

bool DeterministicDrivingPolicy::DecodeWeightBlob(std::span<const std::byte> InBytes, std::string& OutDiagnostic)
{
    if (InBytes.size() != WeightSlabBytes)
    {
        OutDiagnostic = "DeterministicDrivingPolicy weight blob size mismatch (expected 1612 bytes).";
        return false;
    }

    std::array<float, ParameterCount> Decoded{};
    for (std::size_t Idx = 0u; Idx < ParameterCount; ++Idx)
    {
        const float Value = ReadLe32Float(InBytes.data() + (Idx * sizeof(float)));
        if (!std::isfinite(Value))
        {
            OutDiagnostic = "DeterministicDrivingPolicy weight blob contains non-finite float.";
            return false;
        }
        Decoded[Idx] = Value;
    }

    Parameters = Decoded;
    OutDiagnostic.clear();
    return true;
}

bool DeterministicDrivingPolicy::EncodeToDocument(
    std::string_view                  PolicyTitle,
    Serialization::DocumentContainer& OutDocument) const
{
    OutDocument                    = {};
    OutDocument.SuffixCategory     = Serialization::FormatSuffixCategory::BehaviorSpace;
    OutDocument.DocumentTitle      = PolicyTitle.empty() ? std::string("DeterministicDrivingPolicy") : std::string(PolicyTitle);
    OutDocument.AuthoringWorkspace = "Frontier.SimulationSpace";
    OutDocument.StableIdentifier   = "0193b5c0-7a1e-7000-8000-000000000303";
    OutDocument.SchemaVersion      = 1u;

    toml::table Section{};
    Section.insert("Topology",        "16x20x3-Tanh");
    Section.insert("ParameterCount",  static_cast<int64_t>(ParameterCount));
    Section.insert("WeightSlabBytes", static_cast<int64_t>(WeightSlabBytes));
    Section.insert("OriginSeed",      static_cast<int64_t>(OriginSeed));
    Section.insert("ParameterDigest", static_cast<int64_t>(ComputeParameterDigest()));
    OutDocument.TomlHeader.insert("DrivingPolicy", std::move(Section));

    std::array<std::byte, WeightSlabBytes> WeightSlab{};
    (void)EncodeWeightBlob(WeightSlab);

    Serialization::BinarySectionBlock BinarySection{};
    BinarySection.SectionIdentifier = "DrivingPolicyWeights1612";
    BinarySection.SubFormatTag      = ".BehaviorSpace";
    BinarySection.PayloadBytes.assign(WeightSlab.begin(), WeightSlab.end());
    OutDocument.BinarySections.push_back(std::move(BinarySection));
    return true;
}

bool DeterministicDrivingPolicy::DecodeFromDocument(
    const Serialization::DocumentContainer& Document,
    std::string&                            OutDiagnostic)
{
    const auto* WeightSection = Serialization::DocumentContainerDecoder::FindBinarySection(
        Document, "DrivingPolicyWeights1612");
    if (WeightSection == nullptr)
    {
        OutDiagnostic = "Missing 'DrivingPolicyWeights1612' binary section in document.";
        return false;
    }
    return DecodeWeightBlob(WeightSection->PayloadBytes, OutDiagnostic);
}

bool DeterministicDrivingPolicy::RunCrossEntropyEpoch(
    const CrossEntropyTrainingConfig& Config,
    DrivingEpisodeEvaluator           Evaluator,
    void*                             EvaluatorPayload,
    CrossEntropyEpochMetrics&         OutMetrics)
{
    if (Evaluator == nullptr || Config.PopulationSize < 2u || Config.EliteCount == 0u)
    {
        return false;
    }

    const uint32_t Population = std::clamp(Config.PopulationSize, 2u, 256u);
    const uint32_t Elites     = std::clamp(Config.EliteCount, 1u, Population);
    const float    Sigma      = std::clamp(Config.InitialSigma, 0.005f, 2.0f);
    const float    Alpha      = std::clamp(Config.SmoothingAlpha, 0.05f, 1.0f);

    struct CandidateSlot
    {
        DeterministicDrivingPolicy Policy{};
        float                      Score = -1.0e30f;
    };

    std::vector<CandidateSlot> Candidates(Population);
    DeterministicSplitMix64    Generator(Config.EpochSeed);

    // Slot 0 keeps the current incumbent policy so elitist CEM-RL never regresses
    Candidates[0].Policy = *this;
    Candidates[0].Score  = Evaluator(Candidates[0].Policy, EvaluatorPayload);

    for (uint32_t CandidateIdx = 1u; CandidateIdx < Population; ++CandidateIdx)
    {
        Candidates[CandidateIdx].Policy            = *this;
        Candidates[CandidateIdx].Policy.OriginSeed = Config.EpochSeed + CandidateIdx;
        for (std::size_t ParamIdx = 0u; ParamIdx < ParameterCount; ++ParamIdx)
        {
            const float Perturb = Generator.NextApproximateGaussian() * Sigma;
            Candidates[CandidateIdx].Policy.Parameters[ParamIdx] =
                std::clamp(Parameters[ParamIdx] + Perturb, -8.0f, 8.0f);
        }
        Candidates[CandidateIdx].Score = Evaluator(Candidates[CandidateIdx].Policy, EvaluatorPayload);
    }

    std::stable_sort(
        Candidates.begin(),
        Candidates.end(),
        [](const CandidateSlot& Left, const CandidateSlot& Right)
        {
            return Left.Score > Right.Score;
        });

    std::array<float, ParameterCount> EliteMean{};
    float                             EliteScoreSum = 0.0f;
    for (uint32_t EliteIdx = 0u; EliteIdx < Elites; ++EliteIdx)
    {
        EliteScoreSum += Candidates[EliteIdx].Score;
        for (std::size_t ParamIdx = 0u; ParamIdx < ParameterCount; ++ParamIdx)
        {
            EliteMean[ParamIdx] += Candidates[EliteIdx].Policy.Parameters[ParamIdx];
        }
    }

    const float InvElites = 1.0f / static_cast<float>(Elites);
    for (std::size_t ParamIdx = 0u; ParamIdx < ParameterCount; ++ParamIdx)
    {
        EliteMean[ParamIdx] *= InvElites;
    }

    // Measure empirical elite spread & update weights with elitist blend
    double VarianceSum = 0.0;
    for (uint32_t EliteIdx = 0u; EliteIdx < Elites; ++EliteIdx)
    {
        for (std::size_t ParamIdx = 0u; ParamIdx < ParameterCount; ++ParamIdx)
        {
            const double Diff = static_cast<double>(Candidates[EliteIdx].Policy.Parameters[ParamIdx] - EliteMean[ParamIdx]);
            VarianceSum += Diff * Diff;
        }
    }

    for (std::size_t ParamIdx = 0u; ParamIdx < ParameterCount; ++ParamIdx)
    {
        Parameters[ParamIdx] = ((1.0f - Alpha) * Parameters[ParamIdx]) + (Alpha * EliteMean[ParamIdx]);
    }
    OriginSeed = Config.EpochSeed;

    // Preserve best candidate if mean blending scored lower than the single best elite
    const float BlendedScore = Evaluator(*this, EvaluatorPayload);
    if (BlendedScore < Candidates[0].Score)
    {
        Parameters = Candidates[0].Policy.Parameters;
    }

    OutMetrics.BestEpisodeScore = std::max(BlendedScore, Candidates[0].Score);
    OutMetrics.EliteMeanScore   = EliteScoreSum * InvElites;
    OutMetrics.UpdatedSigma     = static_cast<float>(
        std::sqrt(VarianceSum / static_cast<double>(Elites * ParameterCount)));
    OutMetrics.CandidatesTested = Population;
    return true;
}

} // namespace Frontier::SimulationSpace::Policies
