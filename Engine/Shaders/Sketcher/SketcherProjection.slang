/*====================================================================================================================================
                                                       SOLIDARCPROJECTION.SLANG
====================================================================================================================================*/
// 🧩 Shared push-constant ABI, selection-state bits and palette of the Sketcher viewport programs

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUSH CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 128 bytes (the Vulkan guaranteed minimum). Mirrored by SketcherProjectionBlock in SketcherRasterization.h; the
//    compile script asserts the reflected offsets. ViewRight.w = 1 for orthographic projection.
struct SketcherProjection
{
    column_major float4x4 ClipFromWorld;                                                     // [-]  - Projection × view
    float4                ViewRight;                                                         // [-]  - Camera right (world), w: orthographic flag
    float4                ViewUp;                                                            // [-]  - Camera up (world), w: zebra stripe count (0 off)
    float4                ViewForward;                                                       // [-]  - Eye → target (world), w: x-ray face opacity (0 off)
    float4                DisplayStroke;                                                     // [px] - Width, height, edge stroke, edge depth bias
};

// 📝 Grid program: PlaneFromClip unprojects each pixel into grid coordinates (u, v, n) of the active drawing plane
//    (Top/Bottom XY, Front/Back XZ, Right/Left YZ); the grid lies on n = 0. Depth is solved from the homogeneous n,
//    which is linear in device z, so no forward matrix is needed.
struct SketcherGridProjection
{
    column_major float4x4 PlaneFromClip;                                                     // [-]   - Grid coordinates ← clip
    float4                AlongU;                                                            // [0-1] - Colour of the axis running along u (v = 0)
    float4                AlongV;                                                            // [0-1] - Colour of the axis running along v (u = 0)
    float4                Reserved;                                                          // [-]   - Keeps the block at 128 bytes
    float4                ReservedSecond;                                                    // [-]   - Keeps the block at 128 bytes
};

// 📝 Image-plane program: the crop of a picture on the world parallelogram Corner + s·Right + t·Down. Corner.w = opacity,
//    Right.w = mode (0 line art: dark ink drawn light on the viewport, 1 photograph as is), Crop = picture uv rectangle.
struct SketcherImagePlaneProjection
{
    column_major float4x4 ClipFromWorld;                                                     // [-]    - Projection × view
    float4                Corner;                                                            // [len]  - Crop top-left (world), w: opacity
    float4                Right;                                                             // [len]  - Crop width span (world), w: mode
    float4                Down;                                                              // [len]  - Crop height span (world), w: unused
    float4                Crop;                                                              // [0-1]  - u0, v0, u1, v1 in the picture
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-face / per-edge state words (storage buffers, one uint per scene face / edge).
static const uint StateSelected = 1u;                                                        // In the selection
static const uint StateActive   = 2u;                                                        // Last selected (operand in focus)
static const uint StateHovered  = 4u;                                                        // Under the pointer

// 📝 Sketcher browser palette: selection #4099ff, active selection #33ff73, viewport #171b23.
static const float3 SelectedTint = float3(0.251, 0.600, 1.000);
static const float3 ActiveTint   = float3(0.200, 1.000, 0.451);
static const float3 HoverTint    = float3(0.780, 0.880, 1.000);

static const uint IdentityEdgeBit = 0x80000000u;                                             // Identity code: edge flag
static const uint IdentitySectionCap = 0x7FFFFFFFu;                                         // Identity code: section cap (no entity)
// 📝 Bit 16 of a face word: the face belongs to an open sheet, so its back side is never a section cap.
static const uint StateSheet = 0x10000u;
