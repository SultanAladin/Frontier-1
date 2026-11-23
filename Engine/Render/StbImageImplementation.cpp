/*==========================================================================================================================================
                                                        STBIMAGEIMPLEMENTATION.CPP
==========================================================================================================================================*/
// 🧩 Single PNG/JPEG stb_image implementation shared by material ingestion and browser import decoding.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#include <stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
