/*============================================================================================================================================
                                                           WORKSPACEREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Generation-checked WorkspaceRegistry implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/WorkspaceRegistry.h"

namespace Frontier::Workspaces
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 REGISTRY OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

WorkspaceToken WorkspaceRegistry::CreateDocument(
    Codecs::SpaceExtensionCategory Category,
    std::string_view               Title,
    const std::filesystem::path&   InitialPath,
    std::size_t                    DocumentRevisionBudgetBytes)
{
    uint32_t TargetIndex = UINT32_MAX;
    for (uint32_t Index = 0u; Index < static_cast<uint32_t>(Slots.size()); ++Index)
    {
        if (!Slots[Index].Occupied)
        {
            TargetIndex = Index;
            break;
        }
    }

    if (TargetIndex == UINT32_MAX)
    {
        TargetIndex = static_cast<uint32_t>(Slots.size());
        Slots.push_back(DocumentSlot{});
    }

    DocumentSlot& Slot = Slots[TargetIndex];
    if (Slot.Generation == 0u)
    {
        Slot.Generation = 1u;
    }

    WorkspaceToken Token{};
    Token.SlotIndex         = TargetIndex;
    Token.Generation        = Slot.Generation;
    Token.ExtensionCategory = Category;

    Slot.Document = std::make_unique<WorkspaceDocument>(
        Token,
        std::string(Title),
        InitialPath,
        DocumentRevisionBudgetBytes);
    Slot.Occupied = true;
    return Token;
}

WorkspaceToken WorkspaceRegistry::OpenDocumentFromFile(
    const std::filesystem::path& SourcePath,
    std::string&                 OutDiagnostic,
    std::size_t                  DocumentRevisionBudgetBytes)
{
    Codecs::SpaceExtensionCategory Category{};
    std::string                    CanonicalExtension;
    if (!Codecs::SpaceContainerDecoder::ResolveExtensionCaseInsensitive(
            SourcePath.extension().string(),
            Category,
            CanonicalExtension,
            OutDiagnostic))
    {
        return WorkspaceToken{};
    }

    const WorkspaceToken Token = CreateDocument(
        Category,
        SourcePath.stem().string(),
        SourcePath,
        DocumentRevisionBudgetBytes);

    WorkspaceDocument* Document = ResolveDocument(Token);
    if (Document == nullptr || !Document->LoadFromDisk(SourcePath, OutDiagnostic))
    {
        (void)CloseDocument(Token);
        return WorkspaceToken{};
    }
    return Document->QueryToken();
}

bool WorkspaceRegistry::CloseDocument(WorkspaceToken Token) noexcept
{
    if (!Token.VerifyValid() || Token.SlotIndex >= Slots.size())
    {
        return false;
    }
    DocumentSlot& Slot = Slots[Token.SlotIndex];
    if (!Slot.Occupied || Slot.Generation != Token.Generation)
    {
        return false;
    }

    Slot.Document.reset();
    Slot.Occupied = false;
    ++Slot.Generation;
    if (Slot.Generation == 0u)
    {
        Slot.Generation = 1u;
    }
    return true;
}

void WorkspaceRegistry::CloseAllDocuments() noexcept
{
    for (DocumentSlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            Slot.Document.reset();
            Slot.Occupied = false;
            ++Slot.Generation;
            if (Slot.Generation == 0u)
            {
                Slot.Generation = 1u;
            }
        }
    }
}

WorkspaceDocument* WorkspaceRegistry::ResolveDocument(WorkspaceToken Token) noexcept
{
    if (!Token.VerifyValid() || Token.SlotIndex >= Slots.size())
    {
        return nullptr;
    }
    DocumentSlot& Slot = Slots[Token.SlotIndex];
    if (!Slot.Occupied || Slot.Generation != Token.Generation)
    {
        return nullptr;
    }
    return Slot.Document.get();
}

const WorkspaceDocument* WorkspaceRegistry::InspectDocument(WorkspaceToken Token) const noexcept
{
    if (!Token.VerifyValid() || Token.SlotIndex >= Slots.size())
    {
        return nullptr;
    }
    const DocumentSlot& Slot = Slots[Token.SlotIndex];
    if (!Slot.Occupied || Slot.Generation != Token.Generation)
    {
        return nullptr;
    }
    return Slot.Document.get();
}

uint32_t WorkspaceRegistry::QueryOpenDocumentCount() const noexcept
{
    uint32_t Total = 0u;
    for (const DocumentSlot& Slot : Slots)
    {
        if (Slot.Occupied)
        {
            ++Total;
        }
    }
    return Total;
}

std::vector<WorkspaceToken> WorkspaceRegistry::EnumerateOpenTokens() const
{
    std::vector<WorkspaceToken> Result;
    for (const DocumentSlot& Slot : Slots)
    {
        if (Slot.Occupied && Slot.Document != nullptr)
        {
            Result.push_back(Slot.Document->QueryToken());
        }
    }
    return Result;
}

} // namespace Frontier::Workspaces
