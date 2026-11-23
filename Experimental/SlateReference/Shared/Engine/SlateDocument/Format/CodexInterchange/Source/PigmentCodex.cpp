//============================================================================================================================================
//                                                           PIGMENTCODEX.CPP
//============================================================================================================================================
// 🧩 Fixed-white dielectric pigment section translation.

#include "SlateDocument/Format/CodexInterchange/Api/PigmentCodex.h"

#include <cstring>
#include <utility>

namespace Slate
{

namespace
{

constexpr std::uint32_t PigmentInformationSection = 0x464E4950u;   // [-] - `PINF`

void Inscribe32(std::vector<std::uint8_t>& Content, std::uint32_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
        Content.push_back(static_cast<std::uint8_t>(Held >> Shift));
}

void Inscribe64(std::vector<std::uint8_t>& Content, std::uint64_t Held)
{
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Content.push_back(static_cast<std::uint8_t>(Held >> Shift));
}

void InscribeReal(std::vector<std::uint8_t>& Content, double Held)
{
    std::uint64_t Bits = 0u;
    std::memcpy(&Bits, &Held, sizeof(Bits));
    Inscribe64(Content, Bits);
}

bool Extract32(const std::vector<std::uint8_t>& Content, std::size_t& Position, std::uint32_t& Held)
{
    if (Position > Content.size() || Content.size() - Position < 4u)
        return false;

    Held = 0u;
    for (std::uint32_t Shift = 0u; Shift < 32u; Shift += 8u)
        Held |= static_cast<std::uint32_t>(Content[Position++]) << Shift;
    return true;
}

bool Extract64(const std::vector<std::uint8_t>& Content, std::size_t& Position, std::uint64_t& Held)
{
    if (Position > Content.size() || Content.size() - Position < 8u)
        return false;

    Held = 0u;
    for (std::uint32_t Shift = 0u; Shift < 64u; Shift += 8u)
        Held |= static_cast<std::uint64_t>(Content[Position++]) << Shift;
    return true;
}

bool ExtractReal(const std::vector<std::uint8_t>& Content, std::size_t& Position, double& Held)
{
    std::uint64_t Bits = 0u;
    if (!Extract64(Content, Position, Bits))
        return false;

    std::memcpy(&Held, &Bits, sizeof(Held));
    return true;
}

void InscribeRun(std::vector<std::uint8_t>& Content, const std::string& Held)
{
    Inscribe32(Content, static_cast<std::uint32_t>(Held.size()));
    Content.insert(Content.end(), Held.begin(), Held.end());
}

bool ExtractRun(const std::vector<std::uint8_t>& Content, std::size_t& Position, std::string& Held)
{
    std::uint32_t Extent = 0u;
    if (!Extract32(Content, Position, Extent) || Extent > Content.size() - Position)
        return false;

    Held.assign(reinterpret_cast<const char*>(Content.data() + Position), Extent);
    Position += Extent;
    return true;
}

}

Deliver<CodexDocument> PigmentCodexInterchange::EncodePigment(const PigmentCodex& Pigment,
                                                               std::uint64_t       Identity,
                                                               std::uint64_t       Revision) const
{
    std::vector<std::uint8_t> Content;
    InscribeRun(Content, Pigment.Naming);
    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
        InscribeReal(Content, Pigment.BaseColour[Component]);
    InscribeReal(Content, Pigment.Roughness);
    InscribeReal(Content, Pigment.IndexOfRefraction);
    Content.push_back(Pigment.Dielectric ? 1u : 0u);

    CodexSection Information;
    Information.Code = PigmentInformationSection;
    Information.MajorVersion = 1u;
    Information.MinorVersion = 0u;
    Information.Revision = Revision;
    Information.Content = std::move(Content);

    CodexDocument Produced;
    Produced.Profile = CodexProfile::Pigment;
    Produced.Identity = Identity;
    Produced.CurrentRevision = Revision;
    Produced.Sections.push_back(std::move(Information));
    return Deliver<CodexDocument>::Result(Produced);
}

Deliver<PigmentCodex> PigmentCodexInterchange::DecodePigment(const CodexDocument& Document) const
{
    if (Document.Profile != CodexProfile::Pigment)
    {
        return Deliver<PigmentCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the Codex document is not a pigment profile" });
    }

    const CodexSection* Information = nullptr;
    for (const CodexSection& Current : Document.Sections)
    {
        if (Current.Code == PigmentInformationSection)
        {
            Information = &Current;
            break;
        }
    }

    if (Information == nullptr || Information->MajorVersion != 1u)
    {
        return Deliver<PigmentCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the pigment-information section is absent or unsupported" });
    }

    PigmentCodex Produced;
    std::size_t Position = 0u;
    if (!ExtractRun(Information->Content, Position, Produced.Naming))
    {
        return Deliver<PigmentCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the pigment naming is incomplete" });
    }

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        if (!ExtractReal(Information->Content, Position, Produced.BaseColour[Component]))
        {
            return Deliver<PigmentCodex>::Refuse(
                { RefusalReason::ContentUnsupported, "the pigment base colour is incomplete" });
        }
    }

    if (!ExtractReal(Information->Content, Position, Produced.Roughness) ||
        !ExtractReal(Information->Content, Position, Produced.IndexOfRefraction) ||
        Position >= Information->Content.size())
    {
        return Deliver<PigmentCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the pigment surface figures are incomplete" });
    }

    const std::uint8_t Dielectric = Information->Content[Position++];
    if (Dielectric > 1u || Position != Information->Content.size())
    {
        return Deliver<PigmentCodex>::Refuse(
            { RefusalReason::ContentUnsupported, "the pigment dielectric declaration is inconsistent" });
    }

    Produced.Dielectric = Dielectric != 0u;
    return Deliver<PigmentCodex>::Result(Produced);
}

}   // namespace Slate
