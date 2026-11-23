//============================================================================================================================================
//                                                           CARDPROJECTION.WGSL
//============================================================================================================================================
// 🧩 The one mapping between an occupant's local frame and a card's (u, v, depth) — shared by capture and every lookup.

//! include "SceneRecords.wgsl"

//------------------------------------------------------------------------------------------------------------------------
//                                                   CARD PROJECTION
//------------------------------------------------------------------------------------------------------------------------

fn ResolveCardAxis(Axis: u32) -> vec3f
{
    var Outward = vec3f(0.0);
    Outward[Axis >> 1u] = select(1.0, -1.0, (Axis & 1u) == 1u);
    return Outward;
}

// 📐 Card coordinates of a local point: u, v across the face, depth along the inward view direction, all in [0, 1].
//    Depth axis k = Axis/2, u axis (k+1) mod 3, v axis (k+2) mod 3 — capture and lookup share this one mapping.
fn ProjectCardCoordinates(Card: CardRecord, Local: vec3f) -> vec3f
{
    let DepthAxis = Card.Axis >> 1u;
    let AcrossU   = (DepthAxis + 1u) % 3u;
    let AcrossV   = (DepthAxis + 2u) % 3u;
    let Extent    = max(Card.LocalMaximum - Card.LocalMinimum, vec3f(1.0e-5));
    let U = (Local[AcrossU] - Card.LocalMinimum[AcrossU]) / Extent[AcrossU];
    let V = (Local[AcrossV] - Card.LocalMinimum[AcrossV]) / Extent[AcrossV];
    let FromMaximum = (Card.LocalMaximum[DepthAxis] - Local[DepthAxis]) / Extent[DepthAxis];
    let FromMinimum = (Local[DepthAxis] - Card.LocalMinimum[DepthAxis]) / Extent[DepthAxis];
    return vec3f(U, V, select(FromMinimum, FromMaximum, (Card.Axis & 1u) == 0u));
}

fn ReconstructCardLocal(Card: CardRecord, Coordinates: vec3f) -> vec3f
{
    let DepthAxis = Card.Axis >> 1u;
    let AcrossU   = (DepthAxis + 1u) % 3u;
    let AcrossV   = (DepthAxis + 2u) % 3u;
    let Extent    = Card.LocalMaximum - Card.LocalMinimum;
    var Local = vec3f(0.0);
    Local[AcrossU] = Card.LocalMinimum[AcrossU] + Coordinates.x * Extent[AcrossU];
    Local[AcrossV] = Card.LocalMinimum[AcrossV] + Coordinates.y * Extent[AcrossV];
    Local[DepthAxis] = select(Card.LocalMinimum[DepthAxis] + Coordinates.z * Extent[DepthAxis],
                              Card.LocalMaximum[DepthAxis] - Coordinates.z * Extent[DepthAxis], (Card.Axis & 1u) == 0u);
    return Local;
}

fn ResolveCardDepthExtent(Card: CardRecord) -> f32
{
    let Extent = Card.LocalMaximum - Card.LocalMinimum;
    return max(Extent[Card.Axis >> 1u], 1.0e-4);
}
