/*====================================================================================================================================
                                                        SOLIDARCFILEDIALOG.CPP
====================================================================================================================================*/
// 🧩 Native file picker for Construct › Image plane (GetOpenFileNameW on Windows, nothing elsewhere)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherFileDialog.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <vector>
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string BrowseSketcherPicture()
{
#if defined(_WIN32)
    std::vector<wchar_t> Chosen(32768, L'\0');                                              // [-] - Long paths allowed
    OPENFILENAMEW Request{};
    Request.lStructSize = sizeof(Request);
    Request.hwndOwner   = GetActiveWindow();
    Request.lpstrFilter = L"Pictures & ImageSpace (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.ImageSpace)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.ImageSpace\0ImageSpace (*.ImageSpace)\0*.ImageSpace\0All files (*.*)\0*.*\0";
    Request.lpstrFile   = Chosen.data();
    Request.nMaxFile    = static_cast<DWORD>(Chosen.size());
    Request.lpstrTitle  = L"Reference picture for the image plane";
    Request.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetOpenFileNameW(&Request)) return {};
    const int Bytes = WideCharToMultiByte(CP_UTF8, 0, Chosen.data(), -1, nullptr, 0, nullptr, nullptr);
    if (Bytes <= 1) return {};
    std::string Path(static_cast<size_t>(Bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, Chosen.data(), -1, Path.data(), Bytes, nullptr, nullptr);
    return Path;
#else
    return {};
#endif
}

bool SketcherFileDialogAvailableCondition() noexcept
{
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}
