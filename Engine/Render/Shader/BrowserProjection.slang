/*==========================================================================================================================================
                                                          BROWSERPROJECTION.SLANG
==========================================================================================================================================*/
// 🧩 Shared Project Browser preview ABI: column-major clip matrix plus the reference rasterizer's eye/channel/options word.
// 📝 Source: Experimental/Project browser/ReferenceRenderer.js SceneVertexSource/SceneFragmentSource/Drafting sources.

struct BrowserPreviewProjection
{
    column_major float4x4 ClipFromSurface; // GL-style product; the host remaps depth to Vulkan [0,1] on upload.
    float4 EyeChannel;      // Eye.xyz + debug channel (0 lit, 1-4 surface channels, 5 drafting).
    float4 RasterOptions;   // Explode, GridEnabled, Ink, spare.
    float4 ViewDirection;   // Drafting view direction xyz + spare.
};
