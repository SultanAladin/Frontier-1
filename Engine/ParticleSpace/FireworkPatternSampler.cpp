/*============================================================================================================================================
                                                       FIREWORKPATTERNSAMPLER.CPP
============================================================================================================================================*/
// 🧩 Exact 1:1 C++20 3D pyrotechnic shell & SkyText pattern sampler (10 firework patterns + 5x7 bitmap font)
// Reference: Experimental/FluidEditor/Engine/FireworkPatterns.js L1-L623

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/FireworkPatternSampler.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

constexpr std::array<FireworkShapeMetadata, 10u> kFireworkShapes {{
    { FireworkPatternOrdinal::ClassicBouquet, "bouquet",   "ClassicBouquet", "Grand Bouquet (Multi-Shell Pyro)" },
    { FireworkPatternOrdinal::SkyText,        "text",      "SkyText",        "Sky Text (Custom Message)" },
    { FireworkPatternOrdinal::Heart,          "heart",     "Heart",          "Glowing Heart" },
    { FireworkPatternOrdinal::Star,           "star",      "Star",           "5-Point Star" },
    { FireworkPatternOrdinal::Smiley,         "smiley",    "Smiley",         "Hanabi Smiley Face" },
    { FireworkPatternOrdinal::Saturn,         "saturn",    "Saturn",         "Saturn Ringed Planet" },
    { FireworkPatternOrdinal::Crown,          "crown",     "Crown",          "Royal Crown" },
    { FireworkPatternOrdinal::Butterfly,      "butterfly", "Butterfly",      "Iridescent Butterfly" },
    { FireworkPatternOrdinal::Spiral,         "spiral",    "Spiral",         "Spiral Galaxy" },
    { FireworkPatternOrdinal::GrandFinale,    "finale",    "GrandFinale",    "New Year's Grand Finale (Cycle All)" },
}};

// Reference: FireworkPatterns.js L18-L65
struct FontGlyphEntry
{
    uint32_t CodePoint;
    const char* Rows[7];
};

constexpr FontGlyphEntry kFont5x7[] = {
    { 'A', { "01110", "10001", "10001", "11111", "10001", "10001", "10001" } },
    { 'B', { "11110", "10001", "10001", "11110", "10001", "10001", "11110" } },
    { 'C', { "01111", "10000", "10000", "10000", "10000", "10000", "01111" } },
    { 'D', { "11110", "10001", "10001", "10001", "10001", "10001", "11110" } },
    { 'E', { "11111", "10000", "10000", "11110", "10000", "10000", "11111" } },
    { 'F', { "11111", "10000", "10000", "11110", "10000", "10000", "10000" } },
    { 'G', { "01111", "10000", "10000", "10111", "10001", "10001", "01110" } },
    { 'H', { "10001", "10001", "10001", "11111", "10001", "10001", "10001" } },
    { 'I', { "11111", "00100", "00100", "00100", "00100", "00100", "11111" } },
    { 'J', { "00111", "00010", "00010", "00010", "10010", "10010", "01100" } },
    { 'K', { "10001", "10010", "10100", "11000", "10100", "10010", "10001" } },
    { 'L', { "10000", "10000", "10000", "10000", "10000", "10000", "11111" } },
    { 'M', { "10001", "11011", "10101", "10101", "10001", "10001", "10001" } },
    { 'N', { "10001", "11001", "10101", "10011", "10001", "10001", "10001" } },
    { 'O', { "01110", "10001", "10001", "10001", "10001", "10001", "01110" } },
    { 'P', { "11110", "10001", "10001", "11110", "10000", "10000", "10000" } },
    { 'Q', { "01110", "10001", "10001", "10001", "10101", "10010", "01101" } },
    { 'R', { "11110", "10001", "10001", "11110", "10100", "10010", "10001" } },
    { 'S', { "01111", "10000", "10000", "01110", "00001", "00001", "11110" } },
    { 'T', { "11111", "00100", "00100", "00100", "00100", "00100", "00100" } },
    { 'U', { "10001", "10001", "10001", "10001", "10001", "10001", "01110" } },
    { 'V', { "10001", "10001", "10001", "10001", "10001", "01010", "00100" } },
    { 'W', { "10001", "10001", "10001", "10101", "10101", "11011", "10001" } },
    { 'X', { "10001", "10001", "01010", "00100", "01010", "10001", "10001" } },
    { 'Y', { "10001", "10001", "01010", "00100", "00100", "00100", "00100" } },
    { 'Z', { "11111", "00001", "00010", "00100", "01000", "10000", "11111" } },
    { '0', { "01110", "10011", "10101", "10101", "11001", "10001", "01110" } },
    { '1', { "00100", "01100", "00100", "00100", "00100", "00100", "01110" } },
    { '2', { "01110", "10001", "00001", "00110", "01000", "10000", "11111" } },
    { '3', { "11110", "00001", "00001", "01110", "00001", "00001", "11110" } },
    { '4', { "00010", "00110", "01010", "10010", "11111", "00010", "00010" } },
    { '5', { "11111", "10000", "11110", "00001", "00001", "10001", "01110" } },
    { '6', { "01110", "10000", "11110", "10001", "10001", "10001", "01110" } },
    { '7', { "11111", "00001", "00010", "00100", "01000", "01000", "01000" } },
    { '8', { "01110", "10001", "10001", "01110", "10001", "10001", "01110" } },
    { '9', { "01110", "10001", "10001", "01111", "00001", "00010", "01100" } },
    { '!', { "00100", "00100", "00100", "00100", "00100", "00000", "00100" } },
    { '?', { "01110", "10001", "00010", "00100", "00100", "00000", "00100" } },
    { '&', { "01100", "10010", "10100", "01000", "10101", "10010", "01101" } },
    { '+', { "00000", "00100", "00100", "11111", "00100", "00100", "00000" } },
    { '-', { "00000", "00000", "00000", "11111", "00000", "00000", "00000" } },
    { '.', { "00000", "00000", "00000", "00000", "00000", "01100", "01100" } },
    { ':', { "00000", "00100", "00100", "00000", "00100", "00100", "00000" } },
    { 0x2605u, { "00100", "00100", "11111", "01110", "01010", "10001", "00000" } }, // ★
    { 0x2665u, { "01010", "11111", "11111", "11111", "01110", "00100", "00000" } }, // ♥
    { ' ', { "00000", "00000", "00000", "00000", "00000", "00000", "00000" } },
};

constexpr std::array<std::array<std::array<double, 3>, 3>, 6> kShellSwatches {{
    {{ {{ 1.0, 0.86, 0.34 }}, {{ 1.0, 0.24, 0.34 }}, {{ 1.0, 0.96, 0.78 }} }},
    {{ {{ 0.24, 0.66, 1.0 }}, {{ 0.58, 0.96, 1.0 }}, {{ 1.0, 0.88, 0.48 }} }},
    {{ {{ 1.0, 0.22, 0.76 }}, {{ 0.68, 0.32, 1.0 }}, {{ 1.0, 0.92, 0.42 }} }},
    {{ {{ 0.18, 0.98, 0.56 }}, {{ 1.0, 0.84, 0.30 }}, {{ 0.76, 1.0, 0.86 }} }},
    {{ {{ 1.0, 0.36, 0.14 }}, {{ 1.0, 0.74, 0.22 }}, {{ 1.0, 0.98, 0.92 }} }},
    {{ {{ 0.14, 0.94, 0.88 }}, {{ 1.0, 0.46, 0.62 }}, {{ 1.0, 0.95, 0.65 }} }},
}};

constexpr std::array<uint32_t, 8u> kFinaleSequence { 1u, 2u, 5u, 3u, 4u, 6u, 7u, 0u };

[[nodiscard]] double FireworkHash(const double n, const double k) noexcept
{
    const double v = std::sin(n * 12.9898 + k * 78.233) * 43758.5453;
    return v - std::floor(v);
}

[[nodiscard]] const FontGlyphEntry& LookupGlyph(const uint32_t cp) noexcept
{
    const FontGlyphEntry* starFallback = &kFont5x7[43];
    for (const FontGlyphEntry& entry : kFont5x7)
    {
        if (entry.CodePoint == cp) return entry;
    }
    return *starFallback;
}

[[nodiscard]] std::vector<uint32_t> DecodeUtf8UppercaseTrimmed(const std::string_view raw, const size_t maxChars)
{
    std::vector<uint32_t> cps;
    size_t i = 0u;
    while (i < raw.size())
    {
        const unsigned char c = static_cast<unsigned char>(raw[i]);
        uint32_t cp = 0u;
        if (c < 0x80u)
        {
            cp = (c >= 'a' && c <= 'z') ? static_cast<uint32_t>(c - 'a' + 'A') : static_cast<uint32_t>(c);
            i += 1u;
        }
        else if ((c & 0xE0u) == 0xC0u && i + 1u < raw.size())
        {
            cp = ((c & 0x1Fu) << 6u) | (static_cast<unsigned char>(raw[i + 1u]) & 0x3Fu);
            i += 2u;
        }
        else if ((c & 0xF0u) == 0xE0u && i + 2u < raw.size())
        {
            cp = ((c & 0x0Fu) << 12u)
               | ((static_cast<unsigned char>(raw[i + 1u]) & 0x3Fu) << 6u)
               | (static_cast<unsigned char>(raw[i + 2u]) & 0x3Fu);
            i += 3u;
        }
        else if ((c & 0xF8u) == 0xF0u && i + 3u < raw.size())
        {
            cp = ((c & 0x07u) << 18u)
               | ((static_cast<unsigned char>(raw[i + 1u]) & 0x3Fu) << 12u)
               | ((static_cast<unsigned char>(raw[i + 2u]) & 0x3Fu) << 6u)
               | (static_cast<unsigned char>(raw[i + 3u]) & 0x3Fu);
            i += 4u;
        }
        else
        {
            i += 1u;
            continue;
        }
        cps.push_back(cp);
    }

    auto isSpace = [](const uint32_t cp) noexcept { return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n'; };
    size_t start = 0u;
    while (start < cps.size() && isSpace(cps[start])) ++start;
    size_t end = cps.size();
    while (end > start && isSpace(cps[end - 1u])) --end;

    std::vector<uint32_t> trimmed;
    for (size_t k = start; k < end && trimmed.size() < maxChars; ++k)
    {
        trimmed.push_back(cps[k]);
    }
    if (trimmed.empty())
    {
        trimmed = { '2', '0', '2', '6' };
    }
    return trimmed;
}

[[nodiscard]] std::string EncodeUtf8(const std::vector<uint32_t>& cps)
{
    std::string out;
    for (const uint32_t cp : cps)
    {
        if (cp < 0x80u) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800u)
        {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6u)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        else if (cp < 0x10000u)
        {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
}

[[nodiscard]] std::vector<std::vector<uint32_t>> WrapTextLines(const std::string_view rawText)
{
    // Reference: FireworkPatterns.js L92-L106
    const std::vector<uint32_t> clean = DecodeUtf8UppercaseTrimmed(rawText.empty() ? "2026" : rawText, 28u);
    if (clean.size() <= 8u)
    {
        return { clean };
    }

    auto isSpace = [](const uint32_t cp) noexcept { return cp == ' ' || cp == '\t' || cp == '\r' || cp == '\n'; };
    std::vector<std::vector<uint32_t>> words;
    std::vector<uint32_t> word;
    for (const uint32_t cp : clean)
    {
        if (isSpace(cp))
        {
            if (!word.empty())
            {
                words.push_back(std::move(word));
                word.clear();
            }
        }
        else
        {
            word.push_back(cp);
        }
    }
    if (!word.empty()) words.push_back(std::move(word));

    std::vector<std::vector<uint32_t>> lines;
    std::vector<uint32_t> cur;
    for (const auto& w : words)
    {
        if (cur.empty())
        {
            cur = w;
        }
        else if (cur.size() + 1u + w.size() <= 9u)
        {
            cur.push_back(' ');
            cur.insert(cur.end(), w.begin(), w.end());
        }
        else
        {
            lines.push_back(std::move(cur));
            cur = w;
        }
    }
    if (!cur.empty()) lines.push_back(std::move(cur));
    if (lines.size() > 3u) lines.resize(3u);
    return lines;
}

void SampleTextPoints(
    const std::string_view text,
    const uint32_t count,
    const int32_t shellIdx,
    FireworkShellSample& out)
{
    // Reference: FireworkPatterns.js L108-L185
    const auto lines = WrapTextLines(text);
    struct PixelRec { double px; double py; size_t ci; };
    std::vector<PixelRec> pixels;
    constexpr double kLineGap = 9.0;
    const double totalH = static_cast<double>(lines.size() * 7u + (lines.size() - 1u) * 2u);
    double maxW = 1.0;
    for (const auto& line : lines)
    {
        maxW = std::max(maxW, static_cast<double>(line.size() * 6u) - 1.0);
    }

    for (size_t li = 0u; li < lines.size(); ++li)
    {
        const auto& line = lines[li];
        const double lw = static_cast<double>(line.size() * 6u) - 1.0;
        const double yOff = (totalH * 0.5) - static_cast<double>(li) * kLineGap;
        for (size_t ci = 0u; ci < line.size(); ++ci)
        {
            const FontGlyphEntry& glyph = LookupGlyph(line[ci]);
            const double xOff = -lw * 0.5 + static_cast<double>(ci * 6u);
            for (int row = 0; row < 7; ++row)
            {
                for (int col = 0; col < 5; ++col)
                {
                    if (glyph.Rows[row][col] == '1')
                    {
                        pixels.push_back(PixelRec {
                            xOff + static_cast<double>(col) + 0.5,
                            yOff - static_cast<double>(row) - 0.5,
                            ci
                        });
                    }
                }
            }
        }
    }
    if (pixels.empty()) pixels.push_back(PixelRec { 0.0, 0.0, 0u });

    const double normScale = 2.0 / std::max({ maxW, totalH * 1.15, 11.0 });
    const auto& pal = kShellSwatches[static_cast<size_t>(std::abs(shellIdx)) % kShellSwatches.size()];
    const uint32_t textN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.82));

    out.Points.clear();
    out.Points.reserve(count);
    for (uint32_t i = 0u; i < count; ++i)
    {
        if (i < textN)
        {
            const PixelRec& pix = pixels[i % pixels.size()];
            const double seed = static_cast<double>(i) * 17.0 + static_cast<double>(shellIdx) * 31.0;
            const double jx = (FireworkHash(seed, 1.0) - 0.5) * 0.42;
            const double jy = (FireworkHash(seed, 2.0) - 0.5) * 0.42;
            const double jz = (FireworkHash(seed, 3.0) - 0.5) * 0.14;
            const auto& cBlend = (pix.ci % 2u == 0u) ? pal[0] : pal[1];
            const auto& bright = (i % 5u == 0u) ? pal[2] : cBlend;
            out.Points.push_back(FireworkSamplePoint {
                (pix.px + jx) * normScale,
                (pix.py + jy) * normScale,
                jz,
                bright[0], bright[1], bright[2],
                1.0,
                0.015 * FireworkHash(static_cast<double>(i), 4.0)
            });
        }
        else
        {
            const double u = static_cast<double>(i - textN) / static_cast<double>(std::max(1u, count - textN));
            const double ang = u * kPi * 2.0 + static_cast<double>(shellIdx) * 0.4;
            const double rad = 1.12 + 0.16 * FireworkHash(static_cast<double>(i), 5.0);
            const auto& spark = pal[2];
            out.Points.push_back(FireworkSamplePoint {
                std::cos(ang) * rad * 1.18,
                std::sin(ang) * rad * 0.72,
                (FireworkHash(static_cast<double>(i), 6.0) - 0.5) * 0.45,
                spark[0], spark[1], spark[2],
                2.0,
                0.04 + 0.06 * FireworkHash(static_cast<double>(i), 7.0)
            });
        }
    }
    out.IsWillow  = false;
    out.HasStrobe = true;
}

void SampleShapePoints(
    const uint32_t shapeId,
    const uint32_t count,
    const int32_t shellIdx,
    FireworkShellSample& out)
{
    // Reference: FireworkPatterns.js L187-L527
    const auto& pal = kShellSwatches[static_cast<size_t>(std::abs(shellIdx)) % kShellSwatches.size()];
    out.Points.clear();
    out.Points.reserve(count);

    if (shapeId == 0u)
    {
        const int32_t style = std::abs(shellIdx) % 4;
        const bool isWillow = (style == 1 || style == 2);
        const uint32_t outerN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.68));
        const uint32_t coreN  = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.24));
        const double goldenAngle = kPi * (1.0 + std::sqrt(5.0));

        for (uint32_t i = 0u; i < count; ++i)
        {
            if (i < outerN)
            {
                const double k = static_cast<double>(i) + 0.5;
                const double phi = std::acos(1.0 - (2.0 * k) / static_cast<double>(outerN));
                const double theta = goldenAngle * k + static_cast<double>(shellIdx) * 1.3;
                const double r = 0.92 + 0.16 * FireworkHash(static_cast<double>(i) + static_cast<double>(shellIdx) * 13.0, 1.0);
                if (style == 2)
                {
                    const uint32_t frond = i % 14u;
                    const double fPhi = std::acos(1.0 - (2.0 * (static_cast<double>(frond) + 0.5)) / 14.0);
                    const double fTheta = static_cast<double>(frond) * 2.3999632 + static_cast<double>(shellIdx);
                    const double denom = static_cast<double>(std::max(1u, outerN / 14u));
                    const double frac = (static_cast<double>(i / 14u) + 0.5) / denom;
                    constexpr double kSpread = 0.09;
                    const double jx = (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * kSpread;
                    const double jy = (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * kSpread;
                    const double jz = (FireworkHash(static_cast<double>(i), 4.0) - 0.5) * kSpread;
                    const double radP = 0.25 + 0.85 * frac;
                    out.Points.push_back(FireworkSamplePoint {
                        std::sin(fPhi) * std::cos(fTheta) * radP + jx,
                        std::cos(fPhi) * radP + jy,
                        std::sin(fPhi) * std::sin(fTheta) * radP + jz,
                        1.0, 0.82 - 0.18 * frac, 0.28 + 0.25 * (1.0 - frac),
                        1.0, 0.02 * frac
                    });
                    continue;
                }
                if (style == 3)
                {
                    const uint32_t clust = i % 24u;
                    const double cPhi = std::acos(1.0 - (2.0 * (static_cast<double>(clust) + 0.5)) / 24.0);
                    const double cTheta = static_cast<double>(clust) * 2.3999632;
                    constexpr double kOff = 0.14;
                    const double cx = std::sin(cPhi) * std::cos(cTheta) * 0.92;
                    const double cy = std::cos(cPhi) * 0.92;
                    const double cz = std::sin(cPhi) * std::sin(cTheta) * 0.92;
                    const auto& col = (clust % 2u == 0u) ? pal[0] : pal[1];
                    out.Points.push_back(FireworkSamplePoint {
                        cx + (FireworkHash(static_cast<double>(i), 5.0) - 0.5) * kOff * 2.0,
                        cy + (FireworkHash(static_cast<double>(i), 6.0) - 0.5) * kOff * 2.0,
                        cz + (FireworkHash(static_cast<double>(i), 7.0) - 0.5) * kOff * 2.0,
                        col[0], col[1], col[2],
                        1.0, 0.04 * FireworkHash(static_cast<double>(i), 8.0)
                    });
                    continue;
                }
                const std::array<double, 3> col = (style == 1)
                    ? std::array<double, 3> { 1.0, 0.84, 0.32 }
                    : ((i % 3u == 0u) ? pal[2] : pal[0]);
                out.Points.push_back(FireworkSamplePoint {
                    std::sin(phi) * std::cos(theta) * r,
                    std::cos(phi) * r,
                    std::sin(phi) * std::sin(theta) * r,
                    col[0], col[1], col[2],
                    1.0, 0.015 * FireworkHash(static_cast<double>(i), 2.0)
                });
            }
            else if (i < outerN + coreN)
            {
                const uint32_t j = i - outerN;
                const double phi = std::acos(1.0 - (2.0 * (static_cast<double>(j) + 0.5)) / static_cast<double>(coreN));
                const double theta = goldenAngle * static_cast<double>(j);
                const double r = 0.44 + 0.08 * FireworkHash(static_cast<double>(i), 3.0);
                const auto& col = pal[1];
                out.Points.push_back(FireworkSamplePoint {
                    std::sin(phi) * std::cos(theta) * r,
                    std::cos(phi) * r,
                    std::sin(phi) * std::sin(theta) * r,
                    col[0], col[1], col[2],
                    2.0, 0.01
                });
            }
            else
            {
                const double u = static_cast<double>(i - outerN - coreN) / static_cast<double>(std::max(1u, count - outerN - coreN));
                const double ang = u * kPi * 2.0;
                const double r = 1.06 + 0.08 * FireworkHash(static_cast<double>(i), 4.0);
                out.Points.push_back(FireworkSamplePoint {
                    std::cos(ang) * r,
                    (FireworkHash(static_cast<double>(i), 5.0) - 0.5) * 0.14,
                    std::sin(ang) * r,
                    pal[2][0], pal[2][1], pal[2][2],
                    2.0, 0.03
                });
            }
        }
        out.IsWillow  = isWillow;
        out.HasStrobe = (style == 0 || style == 3);
        return;
    }

    if (shapeId == 2u)
    {
        const uint32_t rimN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.72));
        for (uint32_t i = 0u; i < count; ++i)
        {
            if (i < rimN)
            {
                const double t = (static_cast<double>(i) / static_cast<double>(rimN)) * kPi * 2.0;
                const double layer = (i % 3u == 0u) ? 0.76 : 1.0;
                const double sinT = std::sin(t);
                const double hx = 16.0 * (sinT * sinT * sinT) / 16.5;
                const double hy = (13.0 * std::cos(t) - 5.0 * std::cos(2.0 * t) - 2.0 * std::cos(3.0 * t) - std::cos(4.0 * t)) / 16.5 + 0.12;
                const std::array<double, 3> col = (layer < 0.9)
                    ? std::array<double, 3> { 1.0, 0.82, 0.42 }
                    : std::array<double, 3> { 1.0, 0.18 + 0.14 * std::sin(t * 3.0), 0.44 };
                out.Points.push_back(FireworkSamplePoint {
                    hx * layer + (FireworkHash(static_cast<double>(i), 1.0) - 0.5) * 0.05,
                    hy * layer + (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * 0.05,
                    (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * 0.18,
                    col[0], col[1], col[2],
                    1.0, 0.015 * FireworkHash(static_cast<double>(i), 4.0)
                });
            }
            else
            {
                const double t = FireworkHash(static_cast<double>(i), 5.0) * kPi * 2.0;
                const double sc = 0.25 + 0.55 * std::sqrt(FireworkHash(static_cast<double>(i), 6.0));
                const double sinT = std::sin(t);
                const double hx = 16.0 * (sinT * sinT * sinT) / 16.5;
                const double hy = (13.0 * std::cos(t) - 5.0 * std::cos(2.0 * t) - 2.0 * std::cos(3.0 * t) - std::cos(4.0 * t)) / 16.5 + 0.12;
                out.Points.push_back(FireworkSamplePoint {
                    hx * sc, hy * sc, (FireworkHash(static_cast<double>(i), 7.0) - 0.5) * 0.25,
                    1.0, 0.92, 0.72,
                    2.0, 0.03 * FireworkHash(static_cast<double>(i), 8.0)
                });
            }
        }
        out.IsWillow  = false;
        out.HasStrobe = true;
        return;
    }

    if (shapeId == 3u)
    {
        std::array<std::array<double, 2>, 10u> verts {};
        for (uint32_t k = 0u; k < 10u; ++k)
        {
            const double ang = kPi * 0.5 + (static_cast<double>(k) / 10.0) * kPi * 2.0;
            const double r = (k % 2u == 0u) ? 1.05 : 0.44;
            verts[k] = { std::cos(ang) * r, std::sin(ang) * r };
        }
        const uint32_t rimN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.76));
        for (uint32_t i = 0u; i < count; ++i)
        {
            if (i < rimN)
            {
                const uint32_t seg = i % 10u;
                const double u = FireworkHash(static_cast<double>(i), 1.0);
                const auto& a = verts[seg];
                const auto& b = verts[(seg + 1u) % 10u];
                const double layer = (i % 4u == 0u) ? 0.58 : 1.0;
                const std::array<double, 3> col = (layer < 0.8)
                    ? std::array<double, 3> { 0.36, 0.92, 1.0 }
                    : std::array<double, 3> { 1.0, 0.88, 0.30 };
                out.Points.push_back(FireworkSamplePoint {
                    (a[0] + (b[0] - a[0]) * u) * layer + (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * 0.04,
                    (a[1] + (b[1] - a[1]) * u) * layer + (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * 0.04,
                    (FireworkHash(static_cast<double>(i), 4.0) - 0.5) * 0.16,
                    col[0], col[1], col[2],
                    1.0, 0.01
                });
            }
            else
            {
                const double ang = FireworkHash(static_cast<double>(i), 5.0) * kPi * 2.0;
                const double r = 1.15 + 0.14 * FireworkHash(static_cast<double>(i), 6.0);
                out.Points.push_back(FireworkSamplePoint {
                    std::cos(ang) * r, std::sin(ang) * r, (FireworkHash(static_cast<double>(i), 7.0) - 0.5) * 0.32,
                    1.0, 0.98, 0.82,
                    2.0, 0.04
                });
            }
        }
        out.IsWillow  = false;
        out.HasStrobe = true;
        return;
    }

    if (shapeId == 4u)
    {
        const uint32_t faceN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.52));
        const uint32_t eyeN  = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.18));
        for (uint32_t i = 0u; i < count; ++i)
        {
            if (i < faceN)
            {
                const double t = (static_cast<double>(i) / static_cast<double>(faceN)) * kPi * 2.0;
                const double r = 0.96 + (FireworkHash(static_cast<double>(i), 1.0) - 0.5) * 0.07;
                out.Points.push_back(FireworkSamplePoint {
                    std::cos(t) * r, std::sin(t) * r, (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * 0.12,
                    1.0, 0.88, 0.22,
                    1.0, 0.01
                });
            }
            else if (i < faceN + eyeN)
            {
                const double side = (i % 2u == 0u) ? -1.0 : 1.0;
                const double a = FireworkHash(static_cast<double>(i), 3.0) * kPi * 2.0;
                const double r = std::sqrt(FireworkHash(static_cast<double>(i), 4.0)) * 0.14;
                out.Points.push_back(FireworkSamplePoint {
                    side * 0.35 + std::cos(a) * r, 0.30 + std::sin(a) * r, (FireworkHash(static_cast<double>(i), 5.0) - 0.5) * 0.10,
                    0.42, 0.95, 1.0,
                    1.0, 0.01
                });
            }
            else
            {
                const double u = static_cast<double>(i - faceN - eyeN) / static_cast<double>(std::max(1u, count - faceN - eyeN));
                const double ang = -kPi * 0.84 + u * (kPi * 0.68);
                const double r = 0.55 + (FireworkHash(static_cast<double>(i), 6.0) - 0.5) * 0.06;
                out.Points.push_back(FireworkSamplePoint {
                    std::cos(ang) * r, std::sin(ang) * r - 0.02, (FireworkHash(static_cast<double>(i), 7.0) - 0.5) * 0.10,
                    1.0, 0.34, 0.42,
                    1.0, 0.01
                });
            }
        }
        out.IsWillow  = false;
        out.HasStrobe = true;
        return;
    }

    if (shapeId == 5u)
    {
        const uint32_t coreN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.46));
        constexpr double kTilt = 0.42;
        const double cs = std::cos(kTilt);
        const double sn = std::sin(kTilt);
        const double goldenAngle = kPi * (1.0 + std::sqrt(5.0));
        for (uint32_t i = 0u; i < count; ++i)
        {
            if (i < coreN)
            {
                const double k = static_cast<double>(i) + 0.5;
                const double phi = std::acos(1.0 - (2.0 * k) / static_cast<double>(coreN));
                const double theta = goldenAngle * k;
                const double r = 0.50 + 0.05 * FireworkHash(static_cast<double>(i), 1.0);
                out.Points.push_back(FireworkSamplePoint {
                    std::sin(phi) * std::cos(theta) * r,
                    std::cos(phi) * r,
                    std::sin(phi) * std::sin(theta) * r,
                    1.0, 0.72 + 0.18 * std::cos(phi), 0.22,
                    1.0, 0.01
                });
            }
            else
            {
                const uint32_t j = i - coreN;
                const double ang = (static_cast<double>(j) / static_cast<double>(std::max(1u, count - coreN))) * kPi * 2.0;
                const double band = (j % 3u == 0u) ? 1.18 : (0.86 + 0.22 * FireworkHash(static_cast<double>(i), 2.0));
                const double rx = std::cos(ang) * band;
                const double rz = std::sin(ang) * band;
                const double ry = (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * 0.04;
                const double xr = rx * cs - ry * sn;
                const double yr = rx * sn + ry * cs + rz * 0.22;
                const double zr = rz * 0.92;
                const std::array<double, 3> col = (j % 3u == 0u)
                    ? std::array<double, 3> { 0.95, 0.98, 1.0 }
                    : std::array<double, 3> { 0.24, 0.88, 1.0 };
                out.Points.push_back(FireworkSamplePoint {
                    xr, yr, zr,
                    col[0], col[1], col[2],
                    1.0, 0.02
                });
            }
        }
        out.IsWillow  = false;
        out.HasStrobe = true;
        return;
    }

    if (shapeId == 6u)
    {
        constexpr std::array<std::array<double, 2>, 8u> kPoly {{
            { -0.86, -0.52 }, { 0.86, -0.52 }, { 0.96, 0.44 }, { 0.46, -0.02 },
            { 0.0, 0.78 }, { -0.46, -0.02 }, { -0.96, 0.44 }, { -0.86, -0.52 }
        }};
        constexpr std::array<std::array<double, 2>, 3u> kJewels {{
            { 0.0, 0.86 }, { -0.98, 0.52 }, { 0.98, 0.52 }
        }};
        const uint32_t rimN = static_cast<uint32_t>(std::floor(static_cast<double>(count) * 0.80));
        for (uint32_t i = 0u; i < count; ++i)
        {
            if (i < rimN)
            {
                const uint32_t seg = i % 7u;
                const double u = FireworkHash(static_cast<double>(i), 1.0);
                const auto& a = kPoly[seg];
                const auto& b = kPoly[seg + 1u];
                out.Points.push_back(FireworkSamplePoint {
                    a[0] + (b[0] - a[0]) * u + (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * 0.05,
                    a[1] + (b[1] - a[1]) * u + (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * 0.05,
                    (FireworkHash(static_cast<double>(i), 4.0) - 0.5) * 0.16,
                    1.0, 0.84, 0.24,
                    1.0, 0.01
                });
            }
            else
            {
                const auto& j = kJewels[i % 3u];
                const double a = FireworkHash(static_cast<double>(i), 5.0) * kPi * 2.0;
                const double r = std::sqrt(FireworkHash(static_cast<double>(i), 6.0)) * 0.14;
                const std::array<double, 3> col = (i % 3u == 0u)
                    ? std::array<double, 3> { 0.42, 0.94, 1.0 }
                    : std::array<double, 3> { 1.0, 0.22, 0.36 };
                out.Points.push_back(FireworkSamplePoint {
                    j[0] + std::cos(a) * r, j[1] + std::sin(a) * r, (FireworkHash(static_cast<double>(i), 7.0) - 0.5) * 0.14,
                    col[0], col[1], col[2],
                    2.0, 0.02
                });
            }
        }
        out.IsWillow  = false;
        out.HasStrobe = true;
        return;
    }

    if (shapeId == 7u)
    {
        for (uint32_t i = 0u; i < count; ++i)
        {
            const double t = (static_cast<double>(i) / static_cast<double>(count)) * kPi * 2.0;
            const double s12 = std::sin(t / 12.0);
            const double rRaw = std::exp(std::cos(t)) - 2.0 * std::cos(4.0 * t) + std::pow(s12, 5.0);
            const double sc = (i % 5u == 0u) ? 0.65 : 1.0;
            const double bx = std::sin(t) * rRaw * 0.27 * sc;
            const double by = std::cos(t) * rRaw * 0.27 * sc;
            const std::array<double, 3> col = (sc < 0.8)
                ? std::array<double, 3> { 1.0, 0.90, 0.42 }
                : (std::fabs(bx) > 0.45 ? std::array<double, 3> { 0.22, 0.92, 1.0 } : std::array<double, 3> { 0.88, 0.32, 1.0 });
            out.Points.push_back(FireworkSamplePoint {
                bx + (FireworkHash(static_cast<double>(i), 1.0) - 0.5) * 0.04,
                by + (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * 0.04,
                std::fabs(bx) * 0.24 + (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * 0.10,
                col[0], col[1], col[2],
                1.0, 0.015 * FireworkHash(static_cast<double>(i), 4.0)
            });
        }
        out.IsWillow  = false;
        out.HasStrobe = true;
        return;
    }

    // shapeId == 8: Twin Spiral Galaxy
    for (uint32_t i = 0u; i < count; ++i)
    {
        const uint32_t arm = i % 2u;
        const double u = static_cast<double>(i) / static_cast<double>(count);
        const double ang = u * kPi * 5.2 + static_cast<double>(arm) * kPi;
        const double r = 0.08 + std::pow(u, 0.78) * 1.04;
        const std::array<double, 3> col = (u < 0.25)
            ? std::array<double, 3> { 1.0, 0.96, 0.78 }
            : (arm == 0u ? pal[0] : pal[1]);
        out.Points.push_back(FireworkSamplePoint {
            std::cos(ang) * r + (FireworkHash(static_cast<double>(i), 1.0) - 0.5) * 0.06,
            std::sin(ang) * r + (FireworkHash(static_cast<double>(i), 2.0) - 0.5) * 0.06,
            (FireworkHash(static_cast<double>(i), 3.0) - 0.5) * 0.22,
            col[0], col[1], col[2],
            1.0, 0.02 * u
        });
    }
    out.IsWillow  = true;
    out.HasStrobe = true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  SAMPLING & PACKING API
//------------------------------------------------------------------------------------------------------------------------

std::span<const FireworkShapeMetadata> QueryAllFireworkShapes() noexcept
{
    return kFireworkShapes;
}

FireworkShellSample SampleFireworkShell(
    const uint32_t shapeMode,
    const std::string_view text,
    const uint32_t count,
    const int32_t shellIndex)
{
    // Reference: FireworkPatterns.js L529-L546
    const uint32_t rawMode = std::min(shapeMode, 9u);
    const uint32_t activeMode = (rawMode == 9u)
        ? kFinaleSequence[static_cast<size_t>(std::abs(shellIndex)) % kFinaleSequence.size()]
        : rawMode;

    FireworkShellSample sample {};
    sample.ActiveMode = activeMode;
    const std::string_view effectiveText = text.empty() ? std::string_view("2026") : text;

    if (activeMode == 1u)
    {
        SampleTextPoints(effectiveText, count, shellIndex, sample);
        const auto labelCps = DecodeUtf8UppercaseTrimmed(effectiveText, 18u);
        sample.ShellName = "Text \"" + EncodeUtf8(labelCps) + "\"";
    }
    else
    {
        SampleShapePoints(activeMode, count, shellIndex, sample);
        sample.ShellName = std::string(kFireworkShapes[activeMode].Label);
    }
    return sample;
}

std::vector<float> BuildFireworkPackedBuffer(
    const uint32_t shapeMode,
    const std::string_view text,
    const uint32_t capacity,
    const int32_t shellIndex)
{
    // Reference: FireworkPatterns.js L550-L584
    const uint32_t safeCap = std::max(64u, capacity);
    const FireworkShellSample sample = SampleFireworkShell(shapeMode, text, safeCap, shellIndex);
    std::vector<float> buf(static_cast<size_t>(safeCap) * 20u, 0.0f);
    for (uint32_t i = 0u; i < safeCap; ++i)
    {
        const FireworkSamplePoint& pt = sample.Points[i % sample.Points.size()];
        const size_t o = static_cast<size_t>(i) * 20u;
        buf[o + 0u]  = static_cast<float>(pt.X);
        buf[o + 1u]  = static_cast<float>(pt.Y);
        buf[o + 2u]  = static_cast<float>(pt.Z);
        buf[o + 3u]  = static_cast<float>(pt.Role > 0.0 ? pt.Role : 1.0);
        buf[o + 7u]  = 1.0f;
        buf[o + 8u]  = static_cast<float>(pt.R);
        buf[o + 9u]  = static_cast<float>(pt.G);
        buf[o + 10u] = static_cast<float>(pt.B);
        buf[o + 11u] = 1.0f;
        buf[o + 12u] = static_cast<float>(pt.Delay);
        buf[o + 13u] = sample.IsWillow ? 1.0f : 0.0f;
        buf[o + 14u] = sample.HasStrobe ? 1.0f : 0.0f;
        buf[o + 15u] = static_cast<float>(sample.ActiveMode);
        buf[o + 16u] = 1.0f;
        buf[o + 19u] = 1.0f;
    }
    return buf;
}

FireworkAnchorSet SampleFireworkAnchors(
    const FireworkPatternOrdinal pattern,
    const std::string_view text,
    const uint32_t count,
    const int32_t shellIndex)
{
    // Reference: FireworkPatterns.js L595-L622
    const uint32_t safeCount = std::max(64u, count);
    const FireworkShellSample sample = SampleFireworkShell(static_cast<uint32_t>(pattern), text, safeCount, shellIndex);

    FireworkAnchorSet anchors {};
    anchors.Count     = safeCount;
    anchors.ShellName = sample.ShellName;
    anchors.Positions.resize(static_cast<size_t>(safeCount) * 3u);
    anchors.Normals.resize(static_cast<size_t>(safeCount) * 3u);
    anchors.Ridges.resize(safeCount);

    for (uint32_t i = 0u; i < safeCount; ++i)
    {
        const FireworkSamplePoint& pt = sample.Points[i % sample.Points.size()];
        anchors.Positions[i * 3u + 0u] = static_cast<float>(pt.X);
        anchors.Positions[i * 3u + 1u] = static_cast<float>(pt.Y);
        anchors.Positions[i * 3u + 2u] = static_cast<float>(pt.Z);
        anchors.Normals[i * 3u + 0u]   = static_cast<float>(pt.R);
        anchors.Normals[i * 3u + 1u]   = static_cast<float>(pt.G);
        anchors.Normals[i * 3u + 2u]   = static_cast<float>(pt.B);
        anchors.Ridges[i]              = static_cast<float>(pt.Role > 0.0 ? pt.Role : 1.0);
    }
    return anchors;
}

} // namespace Frontier::ParticleSpace
