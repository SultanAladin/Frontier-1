//============================================================================================================================================
//                                                         ATMOSPHEREINTEGRATOR.JS
//============================================================================================================================================
// 🧩 Light direction from the time of day, sun transmittance at the observer, moonlight below the horizon, sky-view refresh.

import { NormalizeVector, ClampScalar } from "./TransformProjection.js";

const SkyViewExtent = [192, 108];   // [texel] - azimuth × elevation

// 📝 Same constants as AtmosphereScattering.wgsl, so the light the shading uses is the light the sky was integrated for.
const GroundRadius     = 6360000.0;
const AtmosphereRadius = 6460000.0;
const RayleighScatter  = [5.802e-6, 13.558e-6, 33.1e-6];
const MieExtinction    = 4.40e-6;
const OzoneAbsorption  = [0.650e-6, 1.881e-6, 0.085e-6];
const ObserverAltitude = 100.0;

export class AtmosphereIntegrator
{
    constructor(Exchange, Shaders)
    {
        this.Exchange = Exchange;
        this.Shaders  = Shaders;
        this.LastKey  = "";
        this.Light    = { Direction: [0, 0, 1], Illuminance: [1, 1, 1], SkyScale: 1.0, NightSky: [0, 0, 0], SunVisible: 1.0 };
    }

    async ConstructResources()
    {
        this.SkyTexture = this.Exchange.ConstructTexture({ label: "Sky view", size: SkyViewExtent, format: "rgba16float",
                                                           usage: GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.TEXTURE_BINDING });
        this.SkyView = this.SkyTexture.createView();
        this.SkySampler = this.Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                           addressModeU: "repeat", addressModeV: "clamp-to-edge" });
        this.UniformRecords = this.Exchange.ConstructRecords(32, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Atmosphere record");
        this.Layout = this.Exchange.ConstructDescriptorLayout(["Uniform", "StorageTexture2D:rgba16float"], GPUShaderStage.COMPUTE, "Atmosphere");
        this.Group = this.Exchange.ConstructDescriptorGroup(this.Layout, [this.UniformRecords, this.SkyView]);
        this.Program = await this.Shaders.ConstructComputeProgram("AtmosphereScattering.wgsl", "IntegrateSkyView", [this.Layout]);
    }

    // 📝 Elevation and azimuth in degrees. Below about −4° the key light hands over to a cool, dim moon opposite the
    //    sun, and a faint night-sky floor keeps escaped rays from returning pure black.
    ResolveLight(ElevationDegrees, AzimuthDegrees, SolarIlluminance)
    {
        const Elevation = ElevationDegrees * Math.PI / 180.0;
        const Azimuth   = AzimuthDegrees * Math.PI / 180.0;
        const Sun = NormalizeVector([Math.cos(Elevation) * Math.cos(Azimuth), Math.cos(Elevation) * Math.sin(Azimuth), Math.sin(Elevation)]);
        const Transmittance = ResolveSunTransmittance(Sun);
        const SunFade  = ClampScalar((ElevationDegrees + 1.0) / 3.0, 0.0, 1.0);
        const MoonFade = ClampScalar((-ElevationDegrees - 4.0) / 6.0, 0.0, 1.0);
        const MoonElevation = 38.0 * Math.PI / 180.0;
        const Moon = NormalizeVector([Math.cos(MoonElevation) * Math.cos(Azimuth + Math.PI),
                                      Math.cos(MoonElevation) * Math.sin(Azimuth + Math.PI), Math.sin(MoonElevation)]);
        const MoonIlluminance = [0.035, 0.045, 0.065].map((Component) => Component * SolarIlluminance / 10.0);
        if (MoonFade > SunFade)
        {
            this.Light.Direction = Moon;
            this.Light.Illuminance = MoonIlluminance.map((Component) => Component * MoonFade);
        }
        else
        {
            this.Light.Direction = Sun;
            this.Light.Illuminance = Transmittance.map((Component) => Component * SolarIlluminance * SunFade);
        }
        this.Light.SunDirection = Sun;
        this.Light.SunVisible = SunFade;
        this.Light.NightSky = [0.0012, 0.0016, 0.0030].map((Component) => Component * (1.0 - SunFade) * SolarIlluminance / 10.0);
        return this.Light;
    }

    RecordSkyView(Encoder, SolarIlluminance)
    {
        const Sun = this.Light.SunDirection;
        const Key = `${Sun.map((Component) => Component.toFixed(4)).join(",")}|${SolarIlluminance.toFixed(3)}`;
        if (Key === this.LastKey)
            return false;
        this.LastKey = Key;
        const Record = new Float32Array(8);
        Record.set(Sun, 0);
        Record[3] = SolarIlluminance;
        Record[4] = ObserverAltitude;
        this.Exchange.Device.queue.writeBuffer(this.UniformRecords, 0, Record);
        const Pass = Encoder.beginComputePass({ label: "Sky view" });
        Pass.setPipeline(this.Program);
        Pass.setBindGroup(0, this.Group);
        Pass.dispatchWorkgroups(Math.ceil(SkyViewExtent[0] / 8), Math.ceil(SkyViewExtent[1] / 8));
        Pass.end();
        return true;
    }
}

// 📐 Transmittance from the observer toward the sun, 64-step quadrature of the extinction along the ray.
function ResolveSunTransmittance(Direction)
{
    const Origin = [0.0, 0.0, GroundRadius + ObserverAltitude];
    const Dot = Origin[2] * Direction[2];
    const Square = Origin[2] * Origin[2];
    const GroundDiscriminant = Dot * Dot - (Square - GroundRadius * GroundRadius);
    if (GroundDiscriminant > 0.0 && (-Dot - Math.sqrt(GroundDiscriminant)) > 0.0)
        return [0.0, 0.0, 0.0];
    const Exit = -Dot + Math.sqrt(Dot * Dot - (Square - AtmosphereRadius * AtmosphereRadius));
    const Samples = 64;
    const Step = Exit / Samples;
    const Depth = [0.0, 0.0, 0.0];
    for (let Sample = 0; Sample < Samples; ++Sample)
    {
        const Travel = (Sample + 0.5) * Step;
        const Point = [Direction[0] * Travel, Direction[1] * Travel, Origin[2] + Direction[2] * Travel];
        const Altitude = Math.hypot(Point[0], Point[1], Point[2]) - GroundRadius;
        const Rayleigh = Math.exp(-Altitude / 8000.0);
        const Mie = Math.exp(-Altitude / 1200.0);
        const Ozone = Math.max(0.0, 1.0 - Math.abs(Altitude - 25000.0) / 15000.0);
        for (let Channel = 0; Channel < 3; ++Channel)
            Depth[Channel] += (RayleighScatter[Channel] * Rayleigh + MieExtinction * Mie + OzoneAbsorption[Channel] * Ozone) * Step;
    }
    return Depth.map((Component) => Math.exp(-Component));
}
