/*==========================================================================================================================================
                                             PATHREPLAYRECONSTRUCTION.SLANG
==========================================================================================================================================*/
// 🧩 Conservative bounded temporal accumulation for replayed opaque diffuse/specular radiance.

#include "PathReplayReservoir.slang"

// 📝 Gather before ResolveReplayReservoirs overwrites the previous frame's position/normal guides.
[shader("compute")][numthreads(64, 1, 1)] void GatherReplayReconstruction(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Retained = 0;
    if ((Extent.Primary.w & 65536u) == 0 && Extent.History.w == 0 && Extent.History.z != 0 &&
        R.PositionHit.w != 0 && R.MotionPositionToken.w > 0 && R.MotionPixel.w != 0 &&
        ReplayMotionConvertible(R.MotionPixel.xy))
    {
        int2 P = int2(floor(R.MotionPixel.xy + .5));
        if (all(P >= 0) && all(P < int2(Extent.History.xy)))
        {
            uint Previous = uint(P.y) * Extent.History.x + uint(P.x);
            float4 Old = OutputColour[ReplaySlot(5, Previous)];
            float4 Position = OutputColour[ReplaySlot(1, Previous)];
            float3 Normal = OutputColour[ReplaySlot(4, Previous)].xyz;
            float4 Material = OutputColour[ReplaySlot(7, Previous)];
            float4 Optics = OutputColour[ReplaySlot(8, Previous)];
            float3 View = OutputColour[ReplaySlot(9, Previous)].xyz;
            float3 Emission = OutputColour[ReplaySlot(10, Previous)].xyz;
            float NormalLength = dot(Normal, Normal), MotionLength = dot(R.MotionNormal.xyz, R.MotionNormal.xyz);
            float3 Delta = Position.xyz - R.MotionPositionToken.xyz;
            float Radius = clamp(R.MotionPixel.z, 0, .25);
            bool CameraOnly = (Extent.Primary.w & 131072u) != 0;
            // 📝 View-independent diffuse colour may cross a camera change only at the exact same world point
            //    and normal. Nearby points can lie on opposite sides of a visibility discontinuity.
            bool PositionCompatible = CameraOnly ? all(Position.xyz == R.PositionHit.xyz) &&
                                                   all(Position.xyz == R.MotionPositionToken.xyz)
                                                 : dot(Delta, Delta) <= Radius * Radius;
            bool NormalCompatible = CameraOnly ? all(Normal == R.NormalRoughness.xyz) &&
                                                 all(Normal == R.MotionNormal.xyz)
                                               : dot(Normal, R.MotionNormal.xyz) >= .99 * sqrt(NormalLength * MotionLength);
            float3 CurrentOptics = float3(R.NormalRoughness.w, R.ViewMetallic.w, R.ResidualSpecular.w);
            bool MaterialCompatible = CameraOnly ? all(Material == R.AlbedoIor) : all(abs(Material - R.AlbedoIor) <= 1e-5);
            bool OpticsCompatible = CameraOnly ? all(Optics.xyz == CurrentOptics) : all(abs(Optics.xyz - CurrentOptics) <= 1e-5);
            bool EmissionCompatible = CameraOnly ? all(Emission == R.ResidualSpecular.xyz)
                                                 : all(abs(Emission - R.ResidualSpecular.xyz) <= 1e-5);
            bool ViewCompatible = CameraOnly || all(abs(View - R.ViewMetallic.xyz) <= 1e-4);
            bool Finite = isfinite(Old.x) && isfinite(Old.y) && isfinite(Old.z) && isfinite(Old.w);
            bool Compatible = Finite && Old.w >= 1 && Old.w <= 16 && all(Old.xyz >= 0) &&
                Position.w == R.MotionPositionToken.w && PositionCompatible &&
                NormalLength > 1e-12 && MotionLength > 1e-12 && NormalCompatible &&
                MaterialCompatible && OpticsCompatible && Optics.w == float(MaterialAddress(R)) &&
                ViewCompatible && EmissionCompatible;
            if (Compatible)
            {
                Retained = Old;
                InterlockedAdd(Counters[23], 1);
            }
        }
    }
    OutputColour[ReplaySlot(6, Pixel)] = Retained;
}

// 📝 A 16-frame running average becomes a bounded exponential average once it reaches the cap.
[shader("compute")][numthreads(64, 1, 1)] void FinalizeReplayReconstruction(uint3 Dispatch : SV_DispatchThreadID)
{
    uint Pixel = Dispatch.x;
    if (Pixel >= Extent.Dimensions.x) return;
    SurfacePoint R = Receivers[Pixel];
    float4 Raw = OutputColour[Pixel], Old = OutputColour[ReplaySlot(6, Pixel)];
    if (R.PositionHit.w == 0)
    {
        OutputColour[ReplaySlot(5, Pixel)] = 0;
        return;
    }
    float Length = Old.w >= 1 ? min(Old.w + 1, 16) : 1;
    float3 Colour = Old.w >= 1 ? lerp(Old.xyz, Raw.xyz, 1 / Length) : Raw.xyz;
    OutputColour[Pixel] = float4(Colour, Raw.w);
    OutputColour[ReplaySlot(5, Pixel)] = float4(Colour, Length);
    OutputColour[ReplaySlot(7, Pixel)] = R.AlbedoIor;
    OutputColour[ReplaySlot(8, Pixel)] = float4(R.NormalRoughness.w, R.ViewMetallic.w, R.ResidualSpecular.w,
                                                float(MaterialAddress(R)));
    OutputColour[ReplaySlot(9, Pixel)] = float4(R.ViewMetallic.xyz, 0);
    OutputColour[ReplaySlot(10, Pixel)] = float4(R.ResidualSpecular.xyz, 0);
}
