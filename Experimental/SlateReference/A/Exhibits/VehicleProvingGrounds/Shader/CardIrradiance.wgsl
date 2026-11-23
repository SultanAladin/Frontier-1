//============================================================================================================================================
//                                                           CARDIRRADIANCE.WGSL
//============================================================================================================================================
// 🧩 The scheduled-tile record shared by the three card lighting passes (direct, radiosity, final).

// 📝 Three passes walk the same scheduled 8×8 card tiles, in this order each frame:
//    ① CardDirect.wgsl    — E_direct = E_sun · max(n·l, 0) · V, V traced through the scene distance field;
//    ② CardRadiosity.wgsl — one site per 4×4 texels gathers 16 cosine rays whose hits read LAST frame's card
//       radiance, so every frame adds a bounce and the depot converges toward multiple bounces;
//    ③ CardFinal.wgsl     — L_card = (albedo/π)·(E_direct + E_indirect) + emission, the radiance tracers read.

struct CardWorkRecord
{
    TileCount    : u32,   // [-]    scheduled tiles this frame
    SiteStride   : u32,   // [site] sites per row of the card texel space
    StaticWeight  : f32,   // [-]    minimum radiosity weight for static cards
    DynamicWeight : f32,   // [-]    minimum radiosity weight for moving cards
}

fn ResolveTileTexel(Assignment: vec2u, Local: vec2u) -> vec2u
{
    return vec2u(Assignment.y & 0xFFFFu, Assignment.y >> 16u) * 8u + Local;
}

fn ResolveWorkIndex(Group: vec3u) -> u32
{
    return Group.x + Group.y * 65535u;
}
