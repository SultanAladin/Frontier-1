//============================================================================================================================================
//                                                     TEXTURETOOLDECLARATION.H
//============================================================================================================================================
// 🧩 Which texturing tool is held, and what one gesture with it declares. SKELETON — see the ⚠️ below.

#pragma once

#include <cstdint>

namespace Slate
{

// ⚠️ **SKELETON.** This header declares the texturing tool VOCABULARY and deliberately nothing else. There
//    is no placement machine here yet, and adding one now would be guesswork: the sketch equivalent was
//    only correct because it was written against a 5 981-line host that already showed what the machine
//    had to do, and no comparable reading of the texturing host has been done.
//
// 🔴 What already exists, and must NOT be restated here when this unit grows:
//      • `SlateCompute/Compute/ImpressionSequence` — `StrokeDeclaration`, `ImpressionSample`,
//        `StrokeArrival`, `SealedStroke`. Stroke accumulation, deferral and the bounded inverse.
//      • `SlateCompute/Compute/EmissionSequence`   — texel emission and sealing.
//      • the texturing panel in `SlateUI/Interface/` — the artist's brush controls and layer stack.
//    A texturing tool DECLARES against those; it does not reimplement them. The one thing none of them
//    answers is which tool the artist is currently holding and what that tool means, which is why this
//    file exists and why it is this short.
//
// 📝 Declared now rather than when it is needed, for one reason learned on the sketch side: a vocabulary
//    with no owner gets declared twice. `SharedCadSketchSubject` and `ParametricSketchSubject` were the same
//    twenty-two members in two files, reconciled by casting through the underlying integer, and no
//    validator noticed for as long as they both existed. Naming the owner before the second copy is
//    written costs one file; unifying two copies afterwards cost a full step.

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS APPLIED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a texturing tool does to the texels it touches.
/// note  🔴 The word is `Texture`, never the retired gesture word — which named what the ARTIST does. What
///        the mechanism does is write texels into a layer through an impression, and whether that reads as
///        texturing, stencilling or masking is presentation. `SymbolDiscipline::Texturing` spells it so.
/// note  📝 These are the operations `ImpressionSequence` can already carry — the enumeration names them,
///        it does not add them. A member is added here only when the compute side can honour it, which is
///        the rule the sketch side broke by offering `DiameterCircle` before anything could commit one.
/// tag   guarantee
enum class TextureSubject : std::uint32_t
{
    None         = 0u,   // [-] - no texturing tool is held
    Deposit      = 1u,   // [-] - writes the declared channels into the layer
    Erase        = 2u,   // [-] - withdraws coverage the layer already holds
    Smudge       = 3u,   // [-] - carries existing texels along the gesture
    Blur         = 4u,   // [-] - convolves what the layer already holds
    Clone        = 5u,   // [-] - deposits texels sampled from a declared offset
    SubjectCount = 6u    // [-] - the closed count, never a subject
};

/// 🧩 How a gesture decides it is finished.
/// note  🔴 Named for `PlacementClosure` in `SketchToolset` and meaning the same thing one discipline over:
///        a gesture, like a placement, is a thing that begins, accumulates, and then either commits or is
///        abandoned. `Continuous` is the ordinary brush — it runs while the contact is held. `Anchored`
///        is the straight-line and shape-fill family, which takes discrete points exactly as a sketch tool
///        does, and is the reason these two units will share a shape rather than diverge.
/// tag   guarantee
enum class GestureClosure : std::uint32_t
{
    Continuous   = 0u,   // [-] - accumulates while the contact is held; seals on release
    Anchored     = 1u,   // [-] - takes discrete contacts, as a sketch placement does
    ClosureCount = 2u    // [-] - the closed count, never a closure
};

/// 🧩 Everything one texturing tool states about how it is applied.
/// note  ⚠️ Deliberately thin. It states what the tool IS, not what the brush is set to — radius, flow,
///        hardness and channel content are `StrokeDeclaration`'s, already, and restating them here would
///        be the beginning of the second copy this file exists to prevent.
/// tag   guarantee, nonallocating, nonthrowing
struct TextureToolDeclaration
{
    GestureClosure  Closure       = GestureClosure::Continuous;  // [-] - how one gesture finishes
    bool            ReadsSurface  = false;                       // [-] - samples the layer it writes into
    bool            WithdrawsOnly = false;                       // [-] - removes coverage, never adds
    const char*     Naming        = "";                          // [-] - static text; what the artist calls it
};

/// 🧩 What one texturing tool declares.
/// in    Subject   [-]  the tool being held
/// out   Declared  [-]  the declaration; an empty naming for `None`
/// note  🔴 `ReadsSurface` is the one that matters to the compute side: a tool that samples the layer it is
///        writing into cannot be resolved from a stale residency, so `ImpressionSequence` must have the
///        textured extents resident before the gesture resolves rather than after. Smudge, blur and clone
///        all read; deposit and erase do not.
/// cost  ✔️
/// tag   api, constexpr, nonallocating, nonthrowing
constexpr TextureToolDeclaration DeclaredTextureTool(TextureSubject Subject)
{
    switch (Subject)
    {
        case TextureSubject::Deposit: return { GestureClosure::Continuous, false, false, "Deposit" };
        case TextureSubject::Erase:   return { GestureClosure::Continuous, false, true,  "Erase" };
        case TextureSubject::Smudge:  return { GestureClosure::Continuous, true,  false, "Smudge" };
        case TextureSubject::Blur:    return { GestureClosure::Continuous, true,  false, "Blur" };
        case TextureSubject::Clone:   return { GestureClosure::Continuous, true,  false, "Clone" };
        case TextureSubject::None:
        case TextureSubject::SubjectCount: break;
    }

    return { GestureClosure::Continuous, false, false, "" };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE NAMING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the artist calls one texturing tool.
/// in    Subject  [-] the tool being named
/// out   Naming   [-] static text; an empty run for `None` and for the closed count
/// note  📝 The one symbol this unit LINKS. Everything above resolves at compile time, and a unit whose
///        every symbol is `constexpr` produces no object worth archiving — which `Build/Construct.ps1`
///        reads as a unit declaring no translation unit, and refuses. Both `Application` and
///        `SlateWorkspace` name `TextureToolset` in their `[link].unit` line, so the archive has to exist.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* TextureToolNaming(TextureSubject Subject);

}   // namespace Slate
