/*==========================================================================================================================================
                                                             MATCAPLOOKUP.SLANG
==========================================================================================================================================*/
// 🧩 Shared MatCap lookup: a camera-space normal → lit-sphere texture coordinates (Frontier MatCapSurface, Sketcher surfaces).

//----------------------------------------------------------------------------------------------------------------------
//                                                TEXTURE COORDINATES
//----------------------------------------------------------------------------------------------------------------------

// 📝 Normalize after interpolation. Scaling first avoids overflow; cancelling normals use a deterministic centre lookup.
//    Positive camera Y maps to the top of a top-row-first texture: uv = (½ + ½x, ½ − ½y).
float2 MatCapCoordinates(float3 ViewNormal)
{
    float  Maximum = max(abs(ViewNormal.x), max(abs(ViewNormal.y), abs(ViewNormal.z)));
    float3 Normal  = Maximum > 1e-20 ? ViewNormal / Maximum : float3(0, 0, 1);
    Normal = normalize(Normal);
    return Normal.xy * float2(0.5, -0.5) + 0.5;
}
