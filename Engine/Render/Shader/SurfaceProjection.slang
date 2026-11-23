/*==========================================================================================================================================
                                                          SURFACEPROJECTION.SLANG
==========================================================================================================================================*/
// 🧩 Shared column-major CAD projection and display-style ABI; reflection and host offset assertions guard every entry.

//----------------------------------------------------------------------------------------------------------------------
//                                              PUSH CONSTANT DECLARATION
//----------------------------------------------------------------------------------------------------------------------

struct SurfaceProjection
{
    column_major float4x4 ClipFromSurface;
    float4 SurfaceColour;
    float4 EdgeColour;
    float4 DisplayStroke;
};
