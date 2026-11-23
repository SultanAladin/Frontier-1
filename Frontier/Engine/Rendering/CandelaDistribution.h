/*==========================================================================================================================================
                                                         CANDELADISTRIBUTION.H
==========================================================================================================================================*/
// 🧩 Explicit Type-C LM-63 candela tables with angular symmetry and relative-peak interpolation.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <istream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Frontier
{
class CandelaDistribution
{
    std::vector<double>              Vertical, Horizontal, Candela;
    double                           Peak = 0;
    static std::pair<size_t, double> Interval(const std::vector<double>& Angles, double Angle)
    {
        if (Angles.size() == 1)
            return {0, 0};
        auto   Upper = std::upper_bound(Angles.begin(), Angles.end(), Angle);
        size_t Lower =
            Upper == Angles.begin() ? 0 : std::min(size_t(Upper - Angles.begin() - 1), Angles.size() - 2);
        return {Lower, std::clamp((Angle - Angles[Lower]) / (Angles[Lower + 1] - Angles[Lower]), 0.0, 1.0)};
    }

  public:
    const std::vector<double>& VerticalAngles() const
    {
        return Vertical;
    }
    const std::vector<double>& HorizontalAngles() const
    {
        return Horizontal;
    }
    const std::vector<double>& CandelaSamples() const
    {
        return Candela;
    }
    double PeakCandela() const
    {
        return Peak;
    }
    bool Loaded() const
    {
        return Peak > 0;
    }
    static CandelaDistribution Read(std::istream& Input)
    {
        std::string            Text;
        std::array<char, 4096> Block{};
        while (Input)
        {
            Input.read(Block.data(), Block.size());
            const size_t Count = static_cast<size_t>(Input.gcount());
            if (Text.size() + Count > 8 * 1024 * 1024)
                throw std::runtime_error("IES file exceeds supported size");
            Text.append(Block.data(), Count);
        }
        if (Input.bad())
            throw std::runtime_error("IES input read failure");
        std::istringstream Lines(Text);
        std::string        Line;
        if (!std::getline(Lines, Line))
            throw std::runtime_error("IES header missing");
        if (!Line.empty() && Line.back() == '\r')
            Line.pop_back();
        if (Line != "IESNA:LM-63-1995" && Line != "IESNA:LM-63-2002" && Line != "IES:LM-63-2019")
            throw std::runtime_error("Supported IES headers: LM-63 1995, 2002 and 2019");
        bool Tilt = false;
        while (std::getline(Lines, Line))
        {
            if (!Line.empty() && Line.back() == '\r')
                Line.pop_back();
            if (Line.rfind("TILT=", 0) == 0)
            {
                if (Line != "TILT=NONE")
                    throw std::runtime_error("IES tilt tables are not supported");
                Tilt = true;
                break;
            }
        }
        if (!Tilt)
            throw std::runtime_error("IES TILT=NONE declaration missing");
        std::string Numbers((std::istreambuf_iterator<char>(Lines)), {});
        std::replace(Numbers.begin(), Numbers.end(), ',', ' ');
        std::istringstream Stream(Numbers);
        auto               Number = [&]() {
            double Result = 0;
            if (!(Stream >> Result) || !std::isfinite(Result))
                throw std::runtime_error("Invalid or truncated IES numeric payload");
            return Result;
        };
        auto Integer = [&](uint32_t Maximum) {
            double N = Number();
            if (N < 1 || N > Maximum || N != std::floor(N))
                throw std::runtime_error("IES integer extent rejected");
            return uint32_t(N);
        };
        (void)Integer(100000);
        (void)Number();
        const double Multiplier = Number();
        auto         V = Integer(4096), H = Integer(4096);
        const double Type = Number(), Units = Number();
        for (int Axis = 0; Axis < 3; ++Axis)
            (void)Number();
        const double Ballast = Number(), Factor = Number();
        (void)Number();
        if (Type != 1 || (Units != 1 && Units != 2) || Multiplier <= 0 || Ballast != 1 || Factor != 1 ||
            size_t(V) * H > 1048576)
            throw std::runtime_error(
                "IES subset requires Type C, valid units, positive scale and unity ballast factors");
        CandelaDistribution Result;
        Result.Vertical.resize(V);
        Result.Horizontal.resize(H);
        Result.Candela.resize(size_t(V) * H);
        for (auto& Angle : Result.Vertical)
            Angle = Number();
        for (auto& Angle : Result.Horizontal)
            Angle = Number();
        auto Increasing = [](const std::vector<double>& A) {
            for (size_t I = 1; I < A.size(); ++I)
                if (A[I] <= A[I - 1])
                    return false;
            return true;
        };
        if (V < 2 || !Increasing(Result.Vertical) || !Increasing(Result.Horizontal) ||
            Result.Vertical.front() < 0 || Result.Vertical.back() > 180 || Result.Horizontal.front() != 0 ||
            !(H == 1 || Result.Horizontal.back() == 90 || Result.Horizontal.back() == 180 ||
              Result.Horizontal.back() == 360))
            throw std::runtime_error("Unsupported or unordered IES angular coverage");
        for (auto& Intensity : Result.Candela)
        {
            Intensity = Number() * Multiplier;
            if (!std::isfinite(Intensity) || Intensity < 0)
                throw std::runtime_error("Invalid IES candela");
            Result.Peak = std::max(Result.Peak, Intensity);
        }
        if (Result.Peak <= 0)
            throw std::runtime_error("Zero IES distribution");
        if (H > 1 && Result.Horizontal.back() == 360)
            for (size_t I = 0; I < V; ++I)
                if (std::abs(Result.Candela[I] - Result.Candela[(H - 1) * V + I]) > Result.Peak * 1e-9)
                    throw std::runtime_error("IES 0/360 seam is inconsistent");
        std::string Extra;
        if (Stream >> Extra)
            throw std::runtime_error("Unexpected trailing IES payload");
        return Result;
    }
    double EvaluateCandela(double θ, double φ) const
    {
        if (!Loaded() || !std::isfinite(θ) || !std::isfinite(φ) || θ < Vertical.front() ||
            θ > Vertical.back())
            return 0;
        φ = std::fmod(φ, 360);
        if (φ < 0)
            φ += 360;
        if (Horizontal.size() > 1)
        {
            if (Horizontal.back() == 90)
            {
                if (φ > 180)
                    φ = 360 - φ;
                if (φ > 90)
                    φ = 180 - φ;
            }
            else if (Horizontal.back() == 180 && φ > 180)
                φ = 360 - φ;
        }
        auto [V, FV] = Interval(Vertical, θ);
        auto [H, FH] = Interval(Horizontal, φ);
        auto Sample  = [&](size_t Column) {
            return Candela[Column * Vertical.size() + V] * (1 - FV) +
                   Candela[Column * Vertical.size() + V + 1] * FV;
        };
        return Sample(H) * (1 - FH) + Sample(std::min(H + 1, Horizontal.size() - 1)) * FH;
    }
    double Relative(double θ, double φ) const
    {
        return Loaded() ? EvaluateCandela(θ, φ) / Peak : 0;
    }
};
} // namespace Frontier
