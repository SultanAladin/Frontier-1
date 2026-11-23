//============================================================================================================================================
//                                                        HOSTENVIRONMENT.H
//============================================================================================================================================
// 🧩 Where a running executable finds things: its shaders beside the binary, the artist's home directory,
//    and what the content browser should show when it is pointed at a folder.
//
// 🔴 ALL THREE WERE DEFINED TWICE — ONCE IN `EditorHost` AND ONCE IN `ParametricSketchHost` — AND TWO OF
//    THE PAIRS HAD ALREADY DRIFTED. `ShaderStreamDirectory` differed only in whitespace, but
//    `PopulateImportDirectory` did not: the parametric host recognised mesh and material-image files and
//    the editor host did not, so the SAME folder listed different importable files depending on which
//    executable opened it. Neither copy was wrong on its own; having two is what made them disagree.
//
// 📝 This is genuinely a runtime concern rather than a workspace one — it does not vary between texturing
//    and sketching, and it is fixed for the life of the process.

#pragma once

#include "SlateUI/Interface/ContentBrowserPanel/Api/ContentBrowserPanel.h"
#include "SlateUI/Interface/InterfaceExchange/Api/InterfaceExchange.h"
#include "SlateVulkan/Device/HostLifecycle/Api/HostLifecycle.h"

#include <filesystem>
#include <string>

namespace Slate
{

/// 🧩 The directory the compiled shaders were written to, beside the running binary.
/// note ⚠️ Derived from the executable's own path rather than the working directory, because a host
///       launched from a debugger or a file manager does not start in its own folder.
/// out  - [-] an empty string when the executable's path cannot be determined
/// cost 🚩
/// tag  api, allocating
std::string ShaderStreamDirectory();

/// 🧩 The artist's home directory.
/// out  - [-] empty when the platform names none
/// cost 🚩
/// tag  api, allocating
std::filesystem::path HomeProfilePath();

/// 🧩 Fills the content browser with what one directory holds.
/// in   Requested  [-]  the directory to list; the literal "Home" resolves to the profile path
/// note 🔴 `Supported` marks what this build can actually OPEN — directories, the two document formats, and
///       every mesh and image format the importers recognise. The editor host's copy of this listed only
///       the document formats, so meshes it could import were shown as unopenable.
/// cost 🚩
/// tag  api, allocating
void PopulateImportDirectory(ContentBrowserConfiguration& Browser, const std::filesystem::path& Requested);

/// 🧩 The `EngineContent` directory the running executable should read from.
/// in   ExecutablePath  [-]  argv[0], or empty if the host does not know its own path
/// note ⚠️ Searched rather than assumed: beside the working directory, beside the binary, one above the
///       binary, then up to eight parents. A host launched from a debugger, an installed location or a
///       build tree all start somewhere different, and none of them is wrong.
/// note 📝 Recognised by `WhiteTeaService.codex` or a `FontArchives` folder being present. Falls back to
///       `./EngineContent` so the caller always has a path to report in a refusal.
/// cost 🚩
/// tag  api, allocating
std::filesystem::path ResolveEngineContentRoot(const std::filesystem::path& ExecutablePath);

/// 🧩 Hands the device the lifecycle produced to the interface layer.
/// note 🔴 `DeviceOffering` and `InterfaceAttachment` ARE THE SAME TEN FIELDS IN THE SAME ORDER, declared
///       twice under two names — so crossing between them needs a function that does nothing but copy.
///       Two hosts each wrote that function. It is not the copy that is wrong, it is that the two types
///       exist; the copy is the smallest honest way to say so until they are unified.
/// note ⚠️ Unifying them is a `SlateVulkan`/`SlateUI` change touching every host, deliberately NOT done
///       here — this pass is about getting behaviour out of hosts, not about redrawing that boundary.
/// cost ✔️

/// 🧩 Whether this build should ask the vendor for validation layers.
/// note 🔴 The answer to "am I a debug build" belongs in ONE place. Each host was writing its own
///       `#ifdef SLATE_DEBUG` around the same assignment, which is three chances to disagree and a
///       compile-time branch the compiler cannot check the inside of. As a `constexpr` function the body
///       is type-checked in every build, and callers read as ordinary code.
/// cost ✔️
/// tag  api, pure, nonallocating, nonthrowing
constexpr bool DiagnosticLayersRequested()
{
#ifdef SLATE_DEBUG
    return true;
#else
    return false;
#endif
}

}   // namespace Slate
