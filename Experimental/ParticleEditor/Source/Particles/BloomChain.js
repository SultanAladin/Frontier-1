// BLOOMCHAIN.JS
// 🧩 HDR glow for additive particles: the scene renders into a half-float target, a bright cut is halved down five
//    times and summed back up (dual-filter blur), then one composite adds the glow, tone-maps (ACES), adds a soft
//    vignette and a little grain, and writes sRGB to the canvas.
// 📝 Particles are additive and can sum far above 1; the glow only looks right when the target keeps those values.

import { THREE } from '../Vehicle/VehicleFacets.js';

const Corner = /* glsl */ `
varying vec2 vUv;
void main() { vUv = position.xy * 0.5 + 0.5; gl_Position = vec4(position.xy, 0.0, 1.0); }`;

// Downsample: 4 diagonal taps at half-texel offsets plus the centre (Marius Bjørge's dual filter).
const Halve = /* glsl */ `
uniform sampler2D uSource; uniform vec2 uTexel; uniform float uCut; varying vec2 vUv;
void main()
{
    vec3 Sum = texture2D(uSource, vUv).rgb * 4.0;
    Sum += texture2D(uSource, vUv + uTexel * vec2(-1.0, -1.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2( 1.0, -1.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2(-1.0,  1.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2( 1.0,  1.0)).rgb;
    Sum /= 8.0;
    float Bright = max(Sum.r, max(Sum.g, Sum.b));
    Sum *= uCut > 0.0 ? smoothstep(uCut, uCut + 0.6, Bright) : 1.0;
    gl_FragColor = vec4(min(Sum, vec3(64.0)), 1.0);
}`;

// Upsample: 8 taps on a ring, added onto the next-larger level.
const Double = /* glsl */ `
uniform sampler2D uSource; uniform vec2 uTexel; uniform float uWeight; varying vec2 vUv;
void main()
{
    vec3 Sum = vec3(0.0);
    Sum += texture2D(uSource, vUv + uTexel * vec2(-2.0, 0.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2( 2.0, 0.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2(0.0, -2.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2(0.0,  2.0)).rgb;
    Sum += texture2D(uSource, vUv + uTexel * vec2(-1.0, -1.0)).rgb * 2.0;
    Sum += texture2D(uSource, vUv + uTexel * vec2( 1.0, -1.0)).rgb * 2.0;
    Sum += texture2D(uSource, vUv + uTexel * vec2(-1.0,  1.0)).rgb * 2.0;
    Sum += texture2D(uSource, vUv + uTexel * vec2( 1.0,  1.0)).rgb * 2.0;
    gl_FragColor = vec4(Sum / 12.0 * uWeight, 1.0);
}`;

const Finish = /* glsl */ `
uniform sampler2D uScene; uniform sampler2D uGlow; uniform float uStrength; uniform float uExposure; uniform float uClock;
uniform vec2 uAspect; varying vec2 vUv;
vec3 Aces(vec3 Colour)
{
    const mat3 Into = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
    const mat3 Back = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
    Colour = Into * Colour;
    Colour = (Colour * (Colour + 0.0245786) - 0.000090537) / (Colour * (0.983729 * Colour + 0.4329510) + 0.238081);
    return clamp(Back * Colour, 0.0, 1.0);
}
vec3 Srgb(vec3 Linear) { return mix(Linear * 12.92, 1.055 * pow(Linear, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, Linear)); }
void main()
{
    vec3 Colour = texture2D(uScene, vUv).rgb + texture2D(uGlow, vUv).rgb * uStrength * 0.6;
    Colour = Aces(Colour * uExposure);
    vec2 Off = (vUv - 0.5) * uAspect;
    Colour *= 1.0 - 0.38 * smoothstep(0.35, 1.15, length(Off));
    float Grain = fract(sin(dot(gl_FragCoord.xy + fract(uClock) * 97.0, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    gl_FragColor = vec4(Srgb(Colour) + Grain / 255.0 * 1.5, 1.0);
}`;

export class BloomChain
{
    constructor(Renderer, Levels = 5)
    {
        this.Renderer = Renderer;
        const Probe = Renderer.getContext();
        const CanFloat = Renderer.capabilities.isWebGL2 && !!Probe.getExtension('EXT_color_buffer_float');
        const Options = { type: CanFloat ? THREE.HalfFloatType : THREE.UnsignedByteType, depthBuffer: false, minFilter: THREE.LinearFilter, magFilter: THREE.LinearFilter };
        this.Scene = new THREE.WebGLRenderTarget(1, 1, { ...Options, depthBuffer: true, samples: 4 });
        this.Steps = Array.from({ length: Levels }, () => new THREE.WebGLRenderTarget(1, 1, Options));
        this.Ups = Array.from({ length: Levels - 1 }, () => new THREE.WebGLRenderTarget(1, 1, Options));
        const Make = (Fragment, Uniforms) => new THREE.ShaderMaterial({ vertexShader: Corner, fragmentShader: Fragment, uniforms: Uniforms, depthTest: false, depthWrite: false });
        this.HalveMaterial = Make(Halve, { uSource: { value: null }, uTexel: { value: new THREE.Vector2() }, uCut: { value: 0 } });
        this.DoubleMaterial = Make(Double, { uSource: { value: null }, uTexel: { value: new THREE.Vector2() }, uWeight: { value: 1 } });
        this.FinishMaterial = Make(Finish, { uScene: { value: null }, uGlow: { value: null }, uStrength: { value: 1 }, uExposure: { value: 1 }, uClock: { value: 0 }, uAspect: { value: new THREE.Vector2(1, 1) } });
        const Triangle = new THREE.BufferGeometry();
        Triangle.setAttribute('position', new THREE.Float32BufferAttribute([-1, -1, 0, 3, -1, 0, -1, 3, 0], 3));
        this.Quad = new THREE.Mesh(Triangle, this.HalveMaterial);
        this.Quad.frustumCulled = false;
        this.Flat = new THREE.Scene();
        this.Flat.add(this.Quad);
        this.FlatCamera = new THREE.OrthographicCamera(-1, 1, 1, -1, 0, 1);
        this.Strength = 1; this.Cut = 1.2; this.Exposure = 1;
    }

    Resize(Width, Height)
    {
        this.Scene.setSize(Width, Height);
        let W = Width, H = Height;
        this.Steps.forEach((Target, Level) =>
        {
            W = Math.max(1, W >> 1); H = Math.max(1, H >> 1);
            Target.setSize(W, H);
            if (Level < this.Ups.length) this.Ups[Level].setSize(W, H);
        });
        this.FinishMaterial.uniforms.uAspect.value.set(Width / Math.max(Width, Height) * 1.6, Height / Math.max(Width, Height) * 1.6);
    }

    Blit(Material, Target)
    {
        this.Quad.material = Material;
        this.Renderer.setRenderTarget(Target);
        this.Renderer.render(this.Flat, this.FlatCamera);
    }

    Render(Scene, Camera, Clock)
    {
        const Renderer = this.Renderer;
        Renderer.setRenderTarget(this.Scene);
        Renderer.clear();
        Renderer.render(Scene, Camera);

        let Source = this.Scene;
        this.Steps.forEach((Target, Level) =>
        {
            this.HalveMaterial.uniforms.uSource.value = Source.texture;
            this.HalveMaterial.uniforms.uTexel.value.set(0.5 / Source.width, 0.5 / Source.height);
            this.HalveMaterial.uniforms.uCut.value = Level === 0 ? this.Cut : 0;
            this.Blit(this.HalveMaterial, Target);
            Source = Target;
        });
        // Walk back up: each level blurs the smaller one and adds it to its own downsample (additive blend).
        this.DoubleMaterial.blending = THREE.AdditiveBlending;
        for (let Level = this.Steps.length - 2; Level >= 0; Level--)
        {
            const Smaller = Level === this.Steps.length - 2 ? this.Steps[Level + 1] : this.Ups[Level + 1];
            const Into = this.Ups[Level];
            this.DoubleMaterial.blending = THREE.NoBlending;
            this.DoubleMaterial.uniforms.uWeight.value = 0.45;
            this.DoubleMaterial.uniforms.uSource.value = this.Steps[Level].texture;
            this.DoubleMaterial.uniforms.uTexel.value.set(0.5 / this.Steps[Level].width, 0.5 / this.Steps[Level].height);
            this.Blit(this.DoubleMaterial, Into);
            this.DoubleMaterial.blending = THREE.AdditiveBlending;
            this.DoubleMaterial.uniforms.uWeight.value = 0.55;
            this.DoubleMaterial.uniforms.uSource.value = Smaller.texture;
            this.DoubleMaterial.uniforms.uTexel.value.set(0.5 / Smaller.width, 0.5 / Smaller.height);
            this.Blit(this.DoubleMaterial, Into);
        }
        const Uniforms = this.FinishMaterial.uniforms;
        Uniforms.uScene.value = this.Scene.texture;
        Uniforms.uGlow.value = this.Ups[0].texture;
        Uniforms.uStrength.value = this.Strength;
        Uniforms.uExposure.value = this.Exposure;
        Uniforms.uClock.value = Clock;
        this.Blit(this.FinishMaterial, null);
    }
}
