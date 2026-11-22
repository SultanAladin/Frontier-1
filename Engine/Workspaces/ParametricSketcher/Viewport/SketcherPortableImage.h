/*====================================================================================================================================
                                                       SOLIDARCPORTABLEIMAGE.H
====================================================================================================================================*/
// 🧩 PNG output of rendered frames and identity maps (stb_image_write), plus a stable FNV-1a digest of pixel arrays

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// RGBA8 rows, top row first. Alpha is forced opaque in the file. Returns false on an I/O failure.
bool WriteSketcherPng(const std::string& Path, uint32_t Width, uint32_t Height, const uint32_t* Pixels);

// 📝 Identity map → false colour: background black, faces in hashed pastel hues, edges white.
[[nodiscard]] std::vector<uint32_t> PaintSketcherIdentity(const uint32_t* Identities, uint32_t Width, uint32_t Height);

// FNV-1a 64 over the pixel words, as 16 hex digits.
[[nodiscard]] std::string DigestSketcherPixels(const uint32_t* Pixels, size_t Count);

} // namespace Frontier::Workspaces::Sketcher
