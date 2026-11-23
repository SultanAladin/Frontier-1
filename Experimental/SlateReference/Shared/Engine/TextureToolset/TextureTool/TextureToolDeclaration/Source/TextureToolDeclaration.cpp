//============================================================================================================================================
//                                                    TEXTURETOOLDECLARATION.CPP
//============================================================================================================================================
// 🧩 The texturing tool vocabulary's one translation unit. SKELETON — see the ⚠️ below.

#include "TextureToolset/TextureTool/TextureToolDeclaration/Api/TextureToolDeclaration.h"

namespace Slate
{

// ⚠️ **SKELETON, AND DELIBERATELY THIN.** The header is `constexpr` end to end: `DeclaredTextureTool`
//    resolves at compile time and needs nothing linked. So this file adds no behaviour, and adding some
//    now would be the guesswork the header refuses — the placement machine is written when the texturing
//    host is lifted and can be read, exactly as the sketch side was.
//
// 🔴 IT EXISTS BECAUSE A UNIT MUST PRODUCE AN ARCHIVE. `Engine/Application/Module.toml` and
//    `Engine/SlateWorkspace/Module.toml` both name `TextureToolset` in their `[link].unit` line, so
//    `link.exe` is handed `TextureToolset.lib` on every host link. `Build/Construct.ps1` refuses a unit
//    with no translation unit outright — it stopped the whole build here, BEFORE `Application`, so the
//    editor was never relinked and a stale executable was run against a fix it did not contain. One real
//    translation unit answers that with no build-script exception and no empty-archive special case, and
//    it behaves identically under `cl.exe` and `g++`.
//
// 📝 What goes here when the unit grows: the non-`constexpr` half of a texturing tool — the gesture
//    machine that says which tool is held, accumulates one gesture and decides it is finished. What must
//    NOT go here is anything `SlateCompute/Compute/ImpressionSequence` or `EmissionSequence` already
//    owns; this unit declares against them and does not restate them.

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE NAMING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the artist calls one texturing tool.
/// in    Subject  [-] the tool being named
/// out   Naming   [-] static text; an empty run for `None` and for the closed count
/// note  📝 A thin wrapper over the header's `constexpr` table, and the one symbol this unit exports.
///        `DeclaredTextureTool` is resolvable at compile time, so nothing else here needs linking — but a
///        unit that exports NOTHING archives to an empty `.lib`, which `lib.exe` and `ar` treat
///        differently and which reads as a build fault rather than as a skeleton.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* TextureToolNaming(TextureSubject Subject)
{
    return DeclaredTextureTool(Subject).Naming;
}

}   // namespace Slate
