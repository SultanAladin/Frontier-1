/*============================================================================================================================================
                                                    VECTORFIELDCATALOGUE.JS
============================================================================================================================================*/
// 🧩 VectorFieldVisualizer simulation engine: VectorField, FlowField & TurbulenceField runtimes, EForceDuration
//    (Continuous/Modulated/Impulse), ForceAttenuation, ReachShapes, all 19 ForceModel laws, 3D curl/divergence,
//    RK4 streamlines, Beaufort/compass tools, SimulationSpace TOML encoder & GPU uniform packer.

import { CompileFieldProgram, DEFAULT_FORMULA_CODE } from './VectorFieldLang.js';

export const Radian = Math.PI / 180;
export const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));
export const Smooth01 = (T) => {
    const C = Clamp(T, 0, 1);
    return C * C * (3 - 2 * C);
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    TAXONOMY & ENUMS
//------------------------------------------------------------------------------------------------------------------------

export const RuntimeSystems = {
    VectorField: {
        Id: 'VectorField',
        Label: 'Vector Fields',
        Subtitle: 'Continuous analytical, radial impulse & electromagnetic forces',
        Colour: '#78d2c6'
    },
    FlowField: {
        Id: 'FlowField',
        Label: 'Flow Fields',
        Subtitle: 'Directed currents, thermals, spline streams, wakes & aerodynamic lift',
        Colour: '#64b5f6'
    },
    TurbulenceField: {
        Id: 'TurbulenceField',
        Label: 'Turbulence Fields',
        Subtitle: 'Divergence-free curl noise, gust emitters, vorticity & Brownian drift',
        Colour: '#d9b56c'
    }
};

export const EForceDuration = ['Continuous', 'Modulated', 'Impulse'];
export const ForceAttenuations = ['SmoothStep', 'Linear', 'InverseSquare', 'None'];
export const ForceDomains = ['LinearWind', 'RadialBurst', 'Attractor', 'Repulsor', 'Vortex', 'CurlNoise', 'FlowStream'];
export const ReachShapes = ['Sphere', 'Column', 'Box'];
export const FlowStreamProfiles = ['Current', 'ThermalUpdraft', 'SlipstreamJet', 'NavigationField', 'VesselWake'];
export const CurlNoiseProfiles = ['CurlNoise3D', 'GustEmitter', 'VorticityConfinement'];
export const MotionModes = ['None', 'LineAB', 'SplineCurve', 'Orbit'];
export const MotionLoopModes = ['PingPong', 'Loop', 'Once'];

export function Attenuate(Curve, Distance, Radius)
{
    if (Radius <= 1e-6 || Distance >= Radius) return 0;
    if (Curve === 'None') return 1;
    const U = Clamp(Distance / Radius, 0, 1);
    if (Curve === 'Linear') return 1 - U;
    if (Curve === 'InverseSquare')
    {
        const Tail = 1 - Smooth01((U - 0.82) / 0.18);
        return (1 / (1 + 9 * U * U)) * Tail;
    }
    const Soft = 1 - U * U;
    return Soft * Soft;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                19-LAW FORCEMODEL CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

export const ForceModels = [
    {
        Id: 'LinearWind',
        GpuId: 0,
        Name: 'Linear Wind',
        System: 'VectorField',
        Domain: 'LinearWind',
        Channel: 'Flow',
        Unit: 'm/s',
        Colour: '#89e0c4',
        Reach: 'Column',
        Equation: 'v = v₀(1 + s·z)·d̂(θ + v·z) + Gust(x,t)',
        Blurb: 'Directional airflow with altitude shear, Ekman veer, traveling gust waves, and 60s Anemometer telemetry.',
        Defaults: { Strength: 14.5, Bearing: 224, Shear: 0.16, Veer: 9, GustStrength: 0.28, GustFrequency: 0.14, Turbulence: 0.34, TurbulenceScale: 46, Radius: 360, Height: 220, HalfX: 300, HalfZ: 300, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Mean Speed', Min: 0, Max: 50, Step: 0.1, Unit: 'm/s', Digits: 1 },
            { Key: 'Bearing', Label: 'Coming From', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'GustStrength', Label: 'Gustiness', Min: 0, Max: 1, Step: 0.01, Unit: '×', Digits: 2 },
            { Key: 'GustFrequency', Label: 'Gust Freq', Min: 0.02, Max: 1.5, Step: 0.01, Unit: 'Hz', Digits: 2 },
            { Key: 'Turbulence', Label: 'Curl Turb', Min: 0, Max: 1, Step: 0.01, Unit: '', Digits: 2 },
            { Key: 'Shear', Label: 'Alt Shear', Min: 0, Max: 1, Step: 0.01, Unit: '/km', Digits: 2 },
            { Key: 'Veer', Label: 'Ekman Veer', Min: -45, Max: 45, Step: 0.5, Unit: '°/km', Digits: 1 }
        ]
    },
    {
        Id: 'FlowStream',
        GpuId: 1,
        Name: 'Flow Stream',
        System: 'FlowField',
        Domain: 'FlowStream',
        Channel: 'Flow',
        Unit: 'm/s',
        Colour: '#64b5f6',
        Reach: 'Box',
        Equation: 'v = S · g(t) · A(r) · d̂_stream',
        Blurb: 'Bounded directed current, thermal updraft, slipstream jet, navigation steering lane, or trailing vessel wake.',
        Defaults: { Strength: 18, Bearing: 72, StreamProfile: 'Current', InnerRadius: 34, Inflow: 6, Circulation: 8, Radius: 240, Height: 160, HalfX: 110, HalfZ: 220, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Stream Speed', Min: 0, Max: 50, Step: 0.2, Unit: 'm/s', Digits: 1 },
            { Key: 'Bearing', Label: 'Flow Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'InnerRadius', Label: 'Core Width', Min: 6, Max: 120, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'Inflow', Label: 'Entrainment', Min: 0, Max: 25, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'Circulation', Label: 'Wake Roll', Min: -25, Max: 25, Step: 0.5, Unit: 'm/s', Digits: 1 }
        ]
    },
    {
        Id: 'Buoyancy',
        GpuId: 2,
        Name: 'Buoyancy',
        System: 'VectorField',
        Domain: 'LinearWind',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#70cf97',
        Reach: 'Column',
        Equation: 'a_y = g · ((ρ_fluid - ρ_body) / ρ_body) · A(r)',
        Blurb: 'Archimedean vertical buoyant lift with altitude density stratification.',
        Defaults: { Strength: 15, FluidDensity: 1.28, BodyDensity: 0.55, Stratification: 0.35, Radius: 185, Height: 210, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Buoyancy Scale', Min: -30, Max: 40, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'FluidDensity', Label: 'Fluid Density', Min: 0.2, Max: 3.0, Step: 0.02, Unit: 'kg/m³', Digits: 2 },
            { Key: 'BodyDensity', Label: 'Body Density', Min: 0.1, Max: 3.0, Step: 0.02, Unit: 'kg/m³', Digits: 2 },
            { Key: 'Stratification', Label: 'Alt Lapse', Min: 0, Max: 1, Step: 0.02, Unit: '', Digits: 2 }
        ]
    },
    {
        Id: 'RadialBurst',
        GpuId: 3,
        Name: 'Radial Burst',
        System: 'VectorField',
        Domain: 'RadialBurst',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#e08368',
        Reach: 'Sphere',
        Equation: 'R(t) = R₀ + v_wave·t · Δv = F·Δt/m',
        Blurb: 'Expanding spherical shockwave impulse propagating outward at wave speed v_wave.',
        Defaults: { Strength: 44, WaveSpeed: 90, Thickness: 26, InitialRadius: 8, Radius: 240, Height: 240, Attenuation: 'InverseSquare', Duration: 'Impulse', Lasts: 5.5, Repeats: 10 },
        Params: [
            { Key: 'Strength', Label: 'Peak Thrust', Min: 0, Max: 100, Step: 1, Unit: 'm/s²', Digits: 0 },
            { Key: 'WaveSpeed', Label: 'Wave Speed', Min: 10, Max: 280, Step: 2, Unit: 'm/s', Digits: 0 },
            { Key: 'Thickness', Label: 'Shell Width', Min: 4, Max: 90, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'InitialRadius', Label: 'Origin R₀', Min: 1, Max: 50, Step: 1, Unit: 'm', Digits: 0 }
        ]
    },
    {
        Id: 'Attractor',
        GpuId: 4,
        Name: 'Point Attractor',
        System: 'VectorField',
        Domain: 'Attractor',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#8eb6e0',
        Reach: 'Sphere',
        Equation: 'a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)',
        Blurb: 'Bounded central sink pulling vectors inward with optional azimuthal swirl.',
        Defaults: { Strength: 20, SwallowRadius: 14, Swirl: 0, Radius: 195, Height: 195, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Inward Pull', Min: 0, Max: 60, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'SwallowRadius', Label: 'Core Softening', Min: 2, Max: 60, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'Swirl', Label: 'Orbit Swirl', Min: -30, Max: 30, Step: 0.5, Unit: 'm/s²', Digits: 1 }
        ]
    },
    {
        Id: 'Repulsor',
        GpuId: 5,
        Name: 'Point Repulsor',
        System: 'VectorField',
        Domain: 'Repulsor',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#d99e6c',
        Reach: 'Sphere',
        Equation: 'a = +S · A(r) · r̂ + ω_swirl · (ŷ × r̂)',
        Blurb: 'Bounded radial deflector pushing mass and streams outward from center.',
        Defaults: { Strength: 22, Swirl: 0, Radius: 185, Height: 185, Attenuation: 'InverseSquare', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Push Force', Min: 0, Max: 60, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'Swirl', Label: 'Deflect Spin', Min: -25, Max: 25, Step: 0.5, Unit: 'm/s²', Digits: 1 }
        ]
    },
    {
        Id: 'AxisAttractor',
        GpuId: 6,
        Name: 'Axis Attractor',
        System: 'VectorField',
        Domain: 'Attractor',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#7bc6db',
        Reach: 'Column',
        Equation: 'a = -S · A(r_⊥) · n̂_⊥ + S_ax · axiŝ',
        Blurb: 'Line-segment cylindrical filament pulling toward a tilted axis with axial drive.',
        Defaults: { Strength: 18, AxialDrive: 9, Swirl: 6, Bearing: 35, AxisTilt: 15, AxisLength: 260, Radius: 180, Height: 220, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Radial Clamp', Min: 0, Max: 50, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'AxialDrive', Label: 'Axial Drive', Min: -30, Max: 30, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'Swirl', Label: 'Helix Twist', Min: -25, Max: 25, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'Bearing', Label: 'Axis Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'AxisTilt', Label: 'Axis Pitch', Min: -75, Max: 75, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'SplineForce',
        GpuId: 7,
        Name: 'Spline Conduit',
        System: 'FlowField',
        Domain: 'FlowStream',
        Channel: 'Flow',
        Unit: 'm/s',
        Colour: '#9db4e2',
        Reach: 'Box',
        Equation: 'a = S · T̂(s*) - k_c · (p - C(s*))',
        Blurb: '3D Hermite-curved conduit guiding trajectories smoothly along an S-bend ribbon.',
        Defaults: { Strength: 20, CenterPull: 14, TubeRadius: 68, CurveArc: 0.65, HeightArc: 32, Bearing: 28, Radius: 240, Height: 160, HalfX: 165, HalfZ: 235, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Guide Speed', Min: 0, Max: 45, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'CenterPull', Label: 'Center Pull', Min: 0, Max: 35, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'CurveArc', Label: 'S-Bend Arc', Min: -1.5, Max: 1.5, Step: 0.05, Unit: '', Digits: 2 },
            { Key: 'HeightArc', Label: 'Arch Lift', Min: -80, Max: 80, Step: 2, Unit: 'm', Digits: 0 },
            { Key: 'Bearing', Label: 'Conduit Heading', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'SurfaceAdhesion',
        GpuId: 8,
        Name: 'Boundary Adhesion',
        System: 'FlowField',
        Domain: 'FlowStream',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#8ec79e',
        Reach: 'Column',
        Equation: 'a = -S · tanh(Δh / σ) · ŷ + Glide · d̂',
        Blurb: 'Coanda-style ground/boundary cling stabilizing motion onto a target altitude layer.',
        Defaults: { Strength: 18, TargetOffset: 16, BandThickness: 28, GlideDrive: 10, Bearing: 90, Radius: 200, Height: 140, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Cling Pull', Min: 0, Max: 45, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'TargetOffset', Label: 'Target Height', Min: 0, Max: 100, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'BandThickness', Label: 'Layer Width σ', Min: 5, Max: 80, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'GlideDrive', Label: 'Surface Glide', Min: 0, Max: 35, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'Bearing', Label: 'Glide Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'Containment',
        GpuId: 9,
        Name: 'Containment Shell',
        System: 'VectorField',
        Domain: 'Attractor',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#81c784',
        Reach: 'Sphere',
        Equation: 'Zero inside r < R_in; Restoring wall across [R_in, R_out]',
        Blurb: 'Quiet interior shell with smooth elastic restoring force wall near the boundary.',
        Defaults: { Strength: 28, InnerRatio: 0.58, Swirl: 4, Radius: 200, Height: 200, Attenuation: 'None', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Wall Stiffness', Min: 0, Max: 60, Step: 1, Unit: 'm/s²', Digits: 0 },
            { Key: 'InnerRatio', Label: 'Quiet Core', Min: 0.1, Max: 0.9, Step: 0.02, Unit: '×R', Digits: 2 },
            { Key: 'Swirl', Label: 'Boundary Swirl', Min: -20, Max: 20, Step: 0.5, Unit: 'm/s²', Digits: 1 }
        ]
    },
    {
        Id: 'Vortex',
        GpuId: 10,
        Name: 'Vortex Column',
        System: 'VectorField',
        Domain: 'Vortex',
        Channel: 'Flow',
        Unit: 'm/s',
        Colour: '#b9a969',
        Reach: 'Column',
        Equation: 'v_θ = S·(2ρ)/(1+ρ²) · v_r(inflow) · v_y(updraft)',
        Blurb: 'Burgers-Rott 3D cyclonic vortex column with eyewall swirl, boundary inflow, and core updraft.',
        Defaults: { Strength: 34, CoreRadius: 28, Spin: 1, Inflow: 9, Updraft: 16, Radius: 220, Height: 280, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Tangential Peak', Min: 0, Max: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'CoreRadius', Label: 'Eyewall Radius', Min: 5, Max: 90, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'Spin', Label: 'Spin Chirality', Min: -1, Max: 1, Step: 2, Unit: '', Digits: 0 },
            { Key: 'Inflow', Label: 'Base Inflow', Min: -20, Max: 30, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'Updraft', Label: 'Eye Updraft', Min: -20, Max: 35, Step: 0.5, Unit: 'm/s', Digits: 1 }
        ]
    },
    {
        Id: 'Torque',
        GpuId: 11,
        Name: 'Torque Field',
        System: 'VectorField',
        Domain: 'Vortex',
        Channel: 'Accelerate',
        Unit: 'rad/s²',
        Colour: '#c8b074',
        Reach: 'Sphere',
        Equation: 'τ = S · A(r) · normalize(axiŝ × r)',
        Blurb: 'Pure rotational angular couple exerting azimuthal spin acceleration around any 3D axis.',
        Defaults: { Strength: 22, Spin: 1, Bearing: 0, AxisTilt: 0, Radius: 170, Height: 170, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Torque Gain', Min: 0, Max: 50, Step: 0.5, Unit: 'rad/s²', Digits: 1 },
            { Key: 'Spin', Label: 'Spin Sign', Min: -1, Max: 1, Step: 2, Unit: '', Digits: 0 },
            { Key: 'Bearing', Label: 'Axis Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'AxisTilt', Label: 'Axis Tilt', Min: -90, Max: 90, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'MagnusForce',
        GpuId: 12,
        Name: 'Magnus Spin',
        System: 'FlowField',
        Domain: 'Vortex',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#74c6b8',
        Reach: 'Sphere',
        Equation: 'a_M = S · A(r) · (ω_spin × v_rel)',
        Blurb: 'Perpendicular aerodynamic deflection proportional to spin-axis cross relative flow.',
        Defaults: { Strength: 16, SpinRate: 5, Bearing: 45, AxisTilt: 15, Radius: 175, Height: 175, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Magnus Coeff', Min: 0, Max: 45, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'SpinRate', Label: 'Spin Rate ω', Min: -15, Max: 15, Step: 0.5, Unit: 'rad/s', Digits: 1 },
            { Key: 'Bearing', Label: 'Stream Heading', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'AxisTilt', Label: 'Spin Tilt', Min: -90, Max: 90, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'Drag',
        GpuId: 13,
        Name: 'Aerodynamic Drag',
        System: 'VectorField',
        Domain: 'LinearWind',
        Channel: 'Damp',
        Unit: '1/s',
        Colour: '#a198cc',
        Reach: 'Box',
        Equation: 'a_d = -(k₁ + k₂·|v|) · A(r) · v_rel',
        Blurb: 'Baffle zone draining kinetic energy exponentially via linear + quadratic fluid damping.',
        Defaults: { Strength: 1.8, QuadraticDrag: 0.14, Bearing: 0, Radius: 145, Height: 140, HalfX: 95, HalfZ: 95, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Linear Damping', Min: 0, Max: 8, Step: 0.05, Unit: '1/s', Digits: 2 },
            { Key: 'QuadraticDrag', Label: 'Quadratic k₂', Min: 0, Max: 1, Step: 0.01, Unit: '/m', Digits: 2 }
        ]
    },
    {
        Id: 'Lift',
        GpuId: 14,
        Name: 'Airfoil Lift',
        System: 'FlowField',
        Domain: 'FlowStream',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#70cfbe',
        Reach: 'Column',
        Equation: 'a_L = C_L(α) · S · A(r) · n̂_lift',
        Blurb: 'Angle-of-attack aerodynamic airfoil lift & induced upwash plume.',
        Defaults: { Strength: 16, AngleOfAttack: 12, LiftCoeff: 1.15, Bearing: 65, Radius: 165, Height: 180, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Dynamic Scale', Min: 0, Max: 45, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'AngleOfAttack', Label: 'AoA α', Min: -25, Max: 35, Step: 0.5, Unit: '°', Digits: 1 },
            { Key: 'LiftCoeff', Label: 'Lift Coeff C_L', Min: 0, Max: 2.5, Step: 0.05, Unit: '', Digits: 2 },
            { Key: 'Bearing', Label: 'Chord Heading', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'CurlNoise',
        GpuId: 15,
        Name: 'Curl Noise ∇×ψ',
        System: 'TurbulenceField',
        Domain: 'CurlNoise',
        Channel: 'Flow',
        Unit: 'm/s',
        Colour: '#d9b56c',
        Reach: 'Sphere',
        Equation: 'v = S · A(r) · (∇ × ψ(p / λ_eddy - v_d·t))',
        Blurb: 'True 3D divergence-free (∇·v = 0) incompressible turbulence, gust emitters & vorticity confinement.',
        Defaults: { Strength: 14, NoiseProfile: 'CurlNoise3D', EddyScale: 52, DriftSpeed: 4.5, Bearing: 45, Seed: 7331, Radius: 215, Height: 200, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Eddy Speed', Min: 0, Max: 40, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'EddyScale', Label: 'Eddy Scale λ', Min: 10, Max: 160, Step: 2, Unit: 'm', Digits: 0 },
            { Key: 'DriftSpeed', Label: 'Advect Drift', Min: 0, Max: 20, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'Bearing', Label: 'Drift Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'BrownianNoise',
        GpuId: 16,
        Name: 'Brownian Drift',
        System: 'TurbulenceField',
        Domain: 'CurlNoise',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#d1c27a',
        Reach: 'Sphere',
        Equation: 'a = S · A(r) · SmoothCellHash(x, y, z, t)',
        Blurb: 'Deterministic tempo-quantized stochastic micro-flutter for dust, pollen & embers.',
        Defaults: { Strength: 11, SpatialCell: 16, TempoHz: 4.0, Seed: 4099, Radius: 170, Height: 170, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Jitter Amp', Min: 0, Max: 35, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'SpatialCell', Label: 'Cell Size', Min: 4, Max: 60, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'TempoHz', Label: 'Tempo Rate', Min: 0.5, Max: 12, Step: 0.25, Unit: 'Hz', Digits: 2 }
        ]
    },
    {
        Id: 'LorentzField',
        GpuId: 17,
        Name: 'Lorentz E + v×B',
        System: 'VectorField',
        Domain: 'Attractor',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#74c8e0',
        Reach: 'Box',
        Equation: 'a = (q/m)·A(r)·(E⃗ + v_drift × B⃗)',
        Blurb: 'Electromagnetic cross-field inducing helical gyro-curvature perpendicular to B⃗.',
        Defaults: { Strength: 16, ChargeMassRatio: 1.4, ElectricBias: 0.35, MagneticStrength: 1.0, Bearing: 30, AxisTilt: 65, HalfX: 150, HalfZ: 150, Radius: 190, Height: 190, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Field Scale', Min: 0, Max: 45, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'ChargeMassRatio', Label: 'q/m Ratio', Min: -4, Max: 4, Step: 0.1, Unit: 'C/kg', Digits: 1 },
            { Key: 'ElectricBias', Label: 'Electric E⃗', Min: 0, Max: 2, Step: 0.05, Unit: '', Digits: 2 },
            { Key: 'MagneticStrength', Label: 'Magnetic B⃗', Min: 0, Max: 2, Step: 0.05, Unit: 'T', Digits: 2 },
            { Key: 'Bearing', Label: 'B⃗ Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'AxisTilt', Label: 'B⃗ Elevation', Min: -90, Max: 90, Step: 1, Unit: '°', Digits: 0 }
        ]
    },
    {
        Id: 'MagneticDipole',
        GpuId: 18,
        Name: 'Magnetic Dipole',
        System: 'VectorField',
        Domain: 'Attractor',
        Channel: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#c68bd9',
        Reach: 'Sphere',
        Equation: 'B(r) = S·[3(m̂·r̂)r̂ - m̂] / max(r, r_c)³',
        Blurb: '3D magnetic dipole tensor generating closed toroidal flux loops between N and S poles.',
        Defaults: { Strength: 18, CoreSoftening: 26, Bearing: 0, AxisTilt: 80, Radius: 190, Height: 190, Attenuation: 'SmoothStep', Duration: 'Continuous' },
        Params: [
            { Key: 'Strength', Label: 'Dipole Moment', Min: 0, Max: 45, Step: 0.5, Unit: 'm/s²', Digits: 1 },
            { Key: 'CoreSoftening', Label: 'Pole Softening', Min: 8, Max: 80, Step: 1, Unit: 'm', Digits: 0 },
            { Key: 'Bearing', Label: 'Moment Azimuth', Min: 0, Max: 360, Step: 1, Unit: '°', Digits: 0 },
            { Key: 'AxisTilt', Label: 'Moment Tilt', Min: -90, Max: 90, Step: 1, Unit: '°', Digits: 0 }
        ]
    }
];

export const ForceModelById = Object.fromEntries(ForceModels.map((M) => [M.Id, M]));

//------------------------------------------------------------------------------------------------------------------------
//                                             METEOROLOGY & BEAUFORT HELPERS
//------------------------------------------------------------------------------------------------------------------------

export const BEAUFORT_BANDS = [
    [0.5, 0, 'Calm', 'smoke rises straight up'],
    [1.5, 1, 'Light air', 'smoke drifts'],
    [3.3, 2, 'Light breeze', 'leaves rustle'],
    [5.5, 3, 'Gentle breeze', 'flags stir, leaves move'],
    [7.9, 4, 'Moderate breeze', 'dust lifts, small branches move'],
    [10.7, 5, 'Fresh breeze', 'small trees sway'],
    [13.8, 6, 'Strong breeze', 'large branches move'],
    [17.1, 7, 'Near gale', 'whole trees in motion'],
    [20.7, 8, 'Gale', 'twigs break off'],
    [24.4, 9, 'Strong gale', 'slates lift'],
    [28.4, 10, 'Storm', 'trees uprooted'],
    [32.6, 11, 'Violent storm', 'widespread storm gusts'],
    [Infinity, 12, 'Hurricane force', 'extreme structural wind load']
];

export function ResolveBeaufort(Speed)
{
    for (const [Lim, Force, Name, Land] of BEAUFORT_BANDS)
    {
        if (Speed < Lim) return { Force, Name, Land };
    }
    return { Force: 12, Name: 'Hurricane force', Land: 'extreme structural wind load' };
}

const COMPASS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
export const CompassOf = (Deg) => COMPASS[Math.round((((Deg % 360) + 360) % 360) / 22.5) % 16];

//------------------------------------------------------------------------------------------------------------------------
//                                              3D DETERMINISTIC GRADIENT NOISE
//------------------------------------------------------------------------------------------------------------------------

function Hash3(I, J, K, Seed)
{
    let H = Math.imul(I, 374761393) ^ Math.imul(J, 668265263) ^ Math.imul(K, 2147483647) ^ Math.imul(Seed, 1274126177);
    H = Math.imul(H ^ (H >>> 13), 1274126177);
    return (H ^ (H >>> 16)) >>> 0;
}

const Grad3 = [
    [1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0],
    [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
    [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1],
    [1, 1, 0], [-1, 1, 0], [0, -1, 1], [0, -1, -1]
];

function Noise3D(X, Y, Z, Seed)
{
    const Ix = Math.floor(X), Iy = Math.floor(Y), Iz = Math.floor(Z);
    const Fx = X - Ix, Fy = Y - Iy, Fz = Z - Iz;
    const Ux = Fx * Fx * Fx * (Fx * (Fx * 6 - 15) + 10);
    const Uy = Fy * Fy * Fy * (Fy * (Fy * 6 - 15) + 10);
    const Uz = Fz * Fz * Fz * (Fz * (Fz * 6 - 15) + 10);
    const Dot = (Cx, Cy, Cz) => {
        const G = Grad3[Hash3(Ix + Cx, Iy + Cy, Iz + Cz, Seed) & 15];
        return G[0] * (Fx - Cx) + G[1] * (Fy - Cy) + G[2] * (Fz - Cz);
    };
    const Lerp = (A, B, T) => A + (B - A) * T;
    const X00 = Lerp(Dot(0, 0, 0), Dot(1, 0, 0), Ux);
    const X10 = Lerp(Dot(0, 1, 0), Dot(1, 1, 0), Ux);
    const X01 = Lerp(Dot(0, 0, 1), Dot(1, 0, 1), Ux);
    const X11 = Lerp(Dot(0, 1, 1), Dot(1, 1, 1), Ux);
    return Lerp(Lerp(X00, X10, Uy), Lerp(X01, X11, Uy), Uz) * 1.15;
}

function EvalCurlPotential(X, Y, Z, Scale, Seed, Out)
{
    const Inv = 1 / Math.max(2, Scale);
    const Px = X * Inv, Py = Y * Inv, Pz = Z * Inv;
    const Eps = 0.002;
    const PsiX = (Ax, Ay, Az) => Noise3D(Ax, Ay, Az, Seed + 11);
    const PsiY = (Ax, Ay, Az) => Noise3D(Ax + 31.7, Ay - 17.3, Az + 11.9, Seed + 29);
    const PsiZ = (Ax, Ay, Az) => Noise3D(Ax - 13.1, Ay + 41.2, Az - 23.4, Seed + 53);

    const dPz_dY = (PsiZ(Px, Py + Eps, Pz) - PsiZ(Px, Py - Eps, Pz)) / (2 * Eps);
    const dPy_dZ = (PsiY(Px, Py, Pz + Eps) - PsiY(Px, Py, Pz - Eps)) / (2 * Eps);
    const dPx_dZ = (PsiX(Px, Py, Pz + Eps) - PsiX(Px, Py, Pz - Eps)) / (2 * Eps);
    const dPz_dX = (PsiZ(Px + Eps, Py, Pz) - PsiZ(Px - Eps, Py, Pz)) / (2 * Eps);
    const dPy_dX = (PsiY(Px + Eps, Py, Pz) - PsiY(Px - Eps, Py, Pz)) / (2 * Eps);
    const dPx_dY = (PsiX(Px, Py + Eps, Pz) - PsiX(Px, Py - Eps, Pz)) / (2 * Eps);

    Out[0] = dPz_dY - dPy_dZ;
    Out[1] = dPx_dZ - dPz_dX;
    Out[2] = dPy_dX - dPx_dY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                             SOURCE & DURATION CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

let SourceSeq = 1;

export function ConstructForce(ModelId = 'LinearWind', Overrides = {})
{
    const Model = ForceModelById[ModelId] || ForceModelById.LinearWind;
    const Defaults = JSON.parse(JSON.stringify(Model.Defaults));
    const Source = Object.assign({
        Token: 'frc_' + (SourceSeq++).toString(36) + Math.floor(Math.random() * 1e5).toString(36),
        Model: Model.Id,
        System: Model.System,
        Domain: Model.Domain,
        Channel: Model.Channel,
        Name: Model.Name,
        Enabled: true,
        Centre: [0, Model.Reach === 'Sphere' ? 18 : 0, 0],
        Reach: Model.Reach,
        Attenuation: Defaults.Attenuation || 'SmoothStep',
        Duration: Defaults.Duration || 'Continuous',
        Radius: Defaults.Radius ?? 180,
        Height: Defaults.Height ?? 180,
        HalfX: Defaults.HalfX ?? 120,
        HalfZ: Defaults.HalfZ ?? 120,
        ModFrequency: 0.25,
        ModDuty: 0.65,
        Begins: 0,
        Lasts: Defaults.Lasts ?? 8,
        Repeats: Defaults.Repeats ?? 14,
        RampIn: Defaults.RampIn ?? 0.2,
        RampOut: Defaults.RampOut ?? 1.5,
        TriggeredAt: Defaults.TriggeredAt ?? 0,
        UseFormula: false,
        FormulaCode: DEFAULT_FORMULA_CODE,
        Swirl: Defaults.Swirl ?? 16,
        Notes: ''
    }, Defaults, Overrides);

    if (Source.Reach === 'Everywhere') Source.Reach = 'Sphere';
    if (!Number.isFinite(Source.Radius) || Source.Radius <= 0) Source.Radius = 180;
    Source.Centre = [...(Source.Centre || [0, 0, 0])];
    Source.RestCentre = [...(Overrides.RestCentre || Source.Centre)];
    const MotionIn = Overrides.Motion || {};
    Source.Motion = {
        Mode: MotionIn.Mode || 'None',
        Loop: MotionIn.Loop || 'PingPong',
        SpeedMps: Number.isFinite(MotionIn.SpeedMps) ? MotionIn.SpeedMps : 40,
        Phase: Number.isFinite(MotionIn.Phase) ? MotionIn.Phase : 0,
        ScrubU: Number.isFinite(MotionIn.ScrubU) ? MotionIn.ScrubU : 0,
        CurrentU: Number.isFinite(MotionIn.CurrentU) ? MotionIn.CurrentU : 0,
        OrbitRadius: Number.isFinite(MotionIn.OrbitRadius) ? MotionIn.OrbitRadius : 110,
        OrbitBob: Number.isFinite(MotionIn.OrbitBob) ? MotionIn.OrbitBob : 18,
        Waypoints: Array.isArray(MotionIn.Waypoints) ? MotionIn.Waypoints.map((Pt) => [Pt[0] || 0, Pt[1] || 0, Pt[2] || 0]) : []
    };
    return Source;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CURVE / LINE / ORBIT MOTION ANIMATION
//------------------------------------------------------------------------------------------------------------------------

export function EnsureSourceMotionWaypoints(Source, TargetMode = Source?.Motion?.Mode)
{
    if (!Source) return [];
    if (!Source.Motion)
    {
        Source.Motion = {
            Mode: TargetMode || 'None', Loop: 'PingPong', SpeedMps: 40, Phase: 0,
            ScrubU: 0, CurrentU: 0, OrbitRadius: 110, OrbitBob: 18, Waypoints: []
        };
    }
    const M = Source.Motion;
    if (TargetMode) M.Mode = TargetMode;
    const C = Source.Centre || [0, 0, 0];
    if (!Array.isArray(M.Waypoints)) M.Waypoints = [];
    if (M.Waypoints.length === 0)
    {
        M.Waypoints.push([Math.round(C[0]), Math.round(C[1]), Math.round(C[2])]);
    }
    if ((M.Mode === 'LineAB' || M.Mode === 'SplineCurve') && M.Waypoints.length < 2)
    {
        const A = M.Waypoints[0];
        M.Waypoints.push([Math.round(A[0] + 180), Math.round(A[1]), Math.round(A[2] + 95)]);
    }
    if (M.Mode === 'SplineCurve' && M.Waypoints.length < 3)
    {
        const A = M.Waypoints[0];
        const B = M.Waypoints[M.Waypoints.length - 1];
        const Mid = [
            Math.round((A[0] + B[0]) * 0.5 - 45),
            Math.round(Math.max(A[1], B[1]) + 42),
            Math.round((A[2] + B[2]) * 0.5 - 70)
        ];
        M.Waypoints.splice(1, 0, Mid);
    }
    return M.Waypoints;
}

export function EvaluateCatmullRom3D(Pts, U)
{
    if (!Pts || Pts.length === 0) return [0, 0, 0];
    if (Pts.length === 1) return [...Pts[0]];
    const T = Clamp(U, 0, 1);
    if (Pts.length === 2)
    {
        const A = Pts[0], B = Pts[1];
        return [A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T];
    }
    const SegCount = Pts.length - 1;
    const Scaled = T * SegCount;
    const Seg = Math.min(SegCount - 1, Math.floor(Scaled));
    const S = Scaled - Seg;
    const S2 = S * S;
    const S3 = S2 * S;

    const GetPt = (Idx) => {
        if (Idx < 0)
        {
            return [2 * Pts[0][0] - Pts[1][0], 2 * Pts[0][1] - Pts[1][1], 2 * Pts[0][2] - Pts[1][2]];
        }
        if (Idx >= Pts.length)
        {
            const L = Pts.length - 1;
            return [2 * Pts[L][0] - Pts[L - 1][0], 2 * Pts[L][1] - Pts[L - 1][1], 2 * Pts[L][2] - Pts[L - 1][2]];
        }
        return Pts[Idx];
    };

    const P0 = GetPt(Seg - 1);
    const P1 = GetPt(Seg);
    const P2 = GetPt(Seg + 1);
    const P3 = GetPt(Seg + 2);

    const Out = [0, 0, 0];
    for (let Ax = 0; Ax < 3; ++Ax)
    {
        Out[Ax] = 0.5 * (
            (2 * P1[Ax]) +
            (-P0[Ax] + P2[Ax]) * S +
            (2 * P0[Ax] - 5 * P1[Ax] + 4 * P2[Ax] - P3[Ax]) * S2 +
            (-P0[Ax] + 3 * P1[Ax] - 3 * P2[Ax] + P3[Ax]) * S3
        );
    }
    return Out;
}

export function EvaluateMotionPosition(SourceOrMotion, U)
{
    const M = SourceOrMotion?.Motion ? SourceOrMotion.Motion : SourceOrMotion;
    const Fallback = SourceOrMotion?.Centre || [0, 0, 0];
    if (!M || M.Mode === 'None') return [...Fallback];
    const Pts = M.Waypoints || [];
    if (Pts.length === 0) return [...Fallback];
    const T = Clamp(U, 0, 1);

    if (M.Mode === 'Orbit')
    {
        const C = Pts[0];
        const R = Math.max(8, M.OrbitRadius ?? 110);
        const Bob = M.OrbitBob ?? 18;
        const Ang = T * Math.PI * 2;
        return [
            C[0] + Math.cos(Ang) * R,
            C[1] + Math.sin(Ang * 2) * Bob,
            C[2] + Math.sin(Ang) * R
        ];
    }
    if (M.Mode === 'LineAB')
    {
        if (Pts.length === 1) return [...Pts[0]];
        if (Pts.length === 2)
        {
            const A = Pts[0], B = Pts[1];
            return [A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T];
        }
        // Piecewise linear polyline across A -> ... -> B
        const SegCount = Pts.length - 1;
        const Scaled = T * SegCount;
        const Seg = Math.min(SegCount - 1, Math.floor(Scaled));
        const F = Scaled - Seg;
        const A = Pts[Seg], B = Pts[Seg + 1];
        return [A[0] + (B[0] - A[0]) * F, A[1] + (B[1] - A[1]) * F, A[2] + (B[2] - A[2]) * F];
    }
    return EvaluateCatmullRom3D(Pts, T);
}

export function SampleMotionPathPoints(SourceOrMotion, Samples = 40)
{
    const M = SourceOrMotion?.Motion ? SourceOrMotion.Motion : SourceOrMotion;
    if (!M || M.Mode === 'None' || !M.Waypoints?.length) return [];
    const N = Math.max(4, Math.round(Samples));
    const Out = [];
    for (let I = 0; I <= N; ++I)
    {
        Out.push(EvaluateMotionPosition(SourceOrMotion, I / N));
    }
    return Out;
}

export function ComputeMotionPathLength(SourceOrMotion)
{
    const M = SourceOrMotion?.Motion ? SourceOrMotion.Motion : SourceOrMotion;
    if (!M || M.Mode === 'None') return 0;
    if (M.Mode === 'Orbit') return 2 * Math.PI * Math.max(8, M.OrbitRadius ?? 110);
    const Pts = SampleMotionPathPoints(SourceOrMotion, 36);
    if (Pts.length < 2) return 0;
    let Len = 0;
    for (let I = 1; I < Pts.length; ++I)
    {
        Len += Math.hypot(Pts[I][0] - Pts[I - 1][0], Pts[I][1] - Pts[I - 1][1], Pts[I][2] - Pts[I - 1][2]);
    }
    return Len;
}

export function ResolveMotionPhase(Motion, SimTime)
{
    if (!Motion || Motion.Mode === 'None') return 0;
    const Len = Math.max(1, ComputeMotionPathLength(Motion));
    const Speed = Math.max(0, Motion.SpeedMps ?? 40);
    const BasePhase = Number.isFinite(Motion.Phase) ? Motion.Phase : 0;
    const Raw = BasePhase + (Math.max(0, SimTime) * Speed) / Len;

    if (Motion.Loop === 'Once') return Clamp(Raw, 0, 1);
    if (Motion.Loop === 'Loop') return ((Raw % 1) + 1) % 1;
    const Mod = ((Raw % 2) + 2) % 2;
    return Mod <= 1 ? Mod : 2 - Mod;
}

export function StepAnimatedSources(Scene, SimTime, IsRunning = true)
{
    if (!Scene?.Sources?.length || !IsRunning) return [];
    const ChangedTokens = [];
    for (const S of Scene.Sources)
    {
        if (!S.Motion || S.Motion.Mode === 'None' || !S.Motion.Waypoints?.length) continue;
        const U = ResolveMotionPhase(S.Motion, SimTime);
        S.Motion.CurrentU = U;
        S.Motion.ScrubU = U;
        const Pos = EvaluateMotionPosition(S, U);
        if (Math.hypot(Pos[0] - S.Centre[0], Pos[1] - S.Centre[1], Pos[2] - S.Centre[2]) > 1e-4)
        {
            S.Centre = Pos;
            ChangedTokens.push(S.Token);
        }
    }
    return ChangedTokens;
}

export function EnsureCompiledFormula(Source, SlotIndex = 0)
{
    if (!Source) return null;
    const Code = Source.FormulaCode || DEFAULT_FORMULA_CODE;
    if (Source._CompiledCache && Source._CompiledCache.Source === Code && Source._CompiledCache.SlotIndex === SlotIndex)
    {
        return Source._CompiledCache;
    }
    const Res = CompileFieldProgram(Code, SlotIndex);
    Res.SlotIndex = SlotIndex;
    Source._CompiledCache = Res;
    return Res;
}

export function ResolveDurationEnvelope(Source, Time)
{
    if (!Source.Enabled) return { Gate: 0, Age: 0, Status: 'Muted' };
    const Mode = Source.Duration || 'Continuous';
    if (Mode === 'Continuous') return { Gate: 1, Age: Time, Status: 'Continuous' };

    if (Mode === 'Modulated')
    {
        const Freq = Math.max(0.02, Source.ModFrequency || 0.25);
        const Wave = 0.5 + 0.5 * Math.sin(2 * Math.PI * Freq * Time);
        const Duty = Clamp(Source.ModDuty ?? 0.65, 0.1, 1.0);
        const Gate = Smooth01((Wave - (1 - Duty) * 0.5) / Math.max(0.15, Duty));
        return { Gate, Age: Time, Status: Gate > 0.05 ? 'Modulating' : 'Trough' };
    }

    const Start = Source.TriggeredAt ?? 0;
    let Local = Time - Start;
    if (Local < 0) return { Gate: 0, Age: 0, Status: 'Armed' };
    if (Source.Repeats > 0 && Source.Lasts > 0) Local %= Source.Repeats;
    const Window = Math.max(0.5, Source.Lasts || 6);
    if (Local > Window) return { Gate: 0, Age: Local, Status: Source.Repeats > 0 ? 'Recharging' : 'Spent' };
    const Attack = Source.RampIn > 1e-4 ? Smooth01(Local / Source.RampIn) : 1;
    const Decay = Source.RampOut > 1e-4 ? 1 - Smooth01((Local - (Window - Source.RampOut)) / Source.RampOut) : 1;
    return { Gate: Clamp(Attack * Decay, 0, 1), Age: Local, Status: 'Impulse Active' };
}

export function ResolveSpatialShare(Source, X, Y, Z)
{
    const Cx = Source.Centre[0], Cy = Source.Centre[1], Cz = Source.Centre[2];
    const Dx = X - Cx, Dy = Y - Cy, Dz = Z - Cz;

    if (Source.Reach === 'Sphere')
    {
        const Dist = Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        return Attenuate(Source.Attenuation, Dist, Source.Radius);
    }
    if (Source.Reach === 'Column')
    {
        const H = Math.max(10, Source.Height || 180);
        if (Dy < -20 || Dy > H * 1.25) return 0;
        const R = Math.hypot(Dx, Dz);
        if (R >= Source.Radius) return 0;
        const VTaper = Dy <= H ? 1 : 1 - Smooth01((Dy - H) / (0.25 * H));
        const BottomTaper = Dy >= 0 ? 1 : 1 - Smooth01(-Dy / 20);
        return Attenuate(Source.Attenuation, R, Source.Radius) * VTaper * BottomTaper;
    }
    const Turn = (Source.Bearing || 0) * Radian;
    const Cos = Math.cos(Turn), Sin = Math.sin(Turn);
    const Lx = Dx * Cos + Dz * Sin;
    const Lz = -Dx * Sin + Dz * Cos;
    const Hx = Math.max(5, Source.HalfX || Source.Radius * 0.65);
    const Hz = Math.max(5, Source.HalfZ || Source.Radius * 0.9);
    const H = Math.max(10, Source.Height || 140);
    const Norm = Math.max(Math.abs(Lx) / Hx, Math.abs(Lz) / Hz, Dy < -12 ? -Dy / 12 : Dy / H);
    return Norm >= 1 ? 0 : Attenuate(Source.Attenuation, Norm, 1);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           3D EVALUATION (ALL 19 FORCEMODELS)
//------------------------------------------------------------------------------------------------------------------------

const CurlScratch = new Float64Array(3);
const RefFlow = [10, 0, 0];

function AxisVectorFromAngles(BearingDeg = 0, TiltDeg = 0)
{
    const Az = BearingDeg * Radian;
    const El = TiltDeg * Radian;
    const CosEl = Math.cos(El);
    return [Math.sin(Az) * CosEl, Math.sin(El), -Math.cos(Az) * CosEl];
}

export function EvaluateSource3D(Source, X, Y, Z, Time, Out)
{
    const Model = ForceModelById[Source.Model];
    if (!Model) return { Active: false, Gate: 0, Share: 0, Age: 0, Mag: 0 };

    const { Gate, Age, Status } = ResolveDurationEnvelope(Source, Time);
    if (Gate <= 1e-5) return { Active: false, Gate: 0, Share: 0, Age, Status, Mag: 0 };

    const Share = ResolveSpatialShare(Source, X, Y, Z);
    if (Share <= 1e-6) return { Active: false, Gate, Share: 0, Age, Status, Mag: 0 };

    const Cx = Source.Centre[0], Cy = Source.Centre[1], Cz = Source.Centre[2];
    const Dx = X - Cx, Dy = Y - Cy, Dz = Z - Cz;

    if (Source.UseFormula)
    {
        const Compiled = EnsureCompiledFormula(Source);
        if (Compiled?.Ok && Compiled.JsFn)
        {
            const Ctx = {
                R: Source.Radius || 180,
                Gate,
                Attenuate: (Dist, MaxR) => Attenuate(Source.Attenuation, Dist, MaxR),
                CurlNoise: (Px, Py, Pz, Sc) => {
                    EvalCurlPotential(Px + Time * 4.2, Py, Pz, Math.max(4, Sc || 48), 7331, CurlScratch);
                    return CurlScratch;
                },
                Noise3: (Px, Py, Pz) => [
                    Noise3D(Px, Py + Time * 1.7, Pz, 7),
                    Noise3D(Px + 13.7, Py, Pz + Time * 1.7, 19),
                    Noise3D(Px, Py + 29.1, Pz - Time * 1.7, 37)
                ]
            };
            const ResVec = Compiled.JsFn(X, Y, Z, Cx, Cy, Cz, Source.Radius || 180, Source.Strength ?? 14, Source.Swirl ?? 16, Time, Ctx);
            const Fx = (ResVec[0] || 0) * Gate;
            const Fy = (ResVec[1] || 0) * Gate;
            const Fz = (ResVec[2] || 0) * Gate;
            Out[0] += Fx;
            Out[1] += Fy;
            Out[2] += Fz;
            return { Active: true, Gate, Share, Age, Status, Mag: Math.hypot(Fx, Fy, Fz) };
        }
    }

    const Gain = Gate * Share;
    let Fx = 0, Fy = 0, Fz = 0;

    switch (Source.Model)
    {
    case 'LinearWind':
    {
        const BlowDir = ((Source.Bearing ?? 224) + 180) * Radian;
        const AltKm = Math.max(0, Y) / 1000;
        const VeerDir = BlowDir + (Source.Veer || 0) * AltKm * Radian;
        const ShearGain = 1 + (Source.Shear || 0) * Math.min(AltKm, 12);
        const DirX = Math.sin(VeerDir), DirZ = -Math.cos(VeerDir);
        const Along = Dx * DirX + Dz * DirZ;
        const Phase = 2 * Math.PI * (Source.GustFrequency || 0.14) * Time - 0.015 * Along;
        const Gust = 0.62 * Math.sin(Phase) + 0.38 * Math.sin(2.13 * Phase + 1.07);
        const Spd = Source.Strength * ShearGain * (1 + (Source.GustStrength || 0) * Gust);
        Fx = DirX * Spd;
        Fz = DirZ * Spd;
        if ((Source.Turbulence || 0) > 0)
        {
            const Scale = Math.max(8, Source.TurbulenceScale || 46);
            const Drift = 0.35 * Source.Strength * Time;
            EvalCurlPotential(X - DirX * Drift, Y, Z - DirZ * Drift, Scale, 3109, CurlScratch);
            const TG = Source.Turbulence * Source.Strength * 0.42;
            Fx += CurlScratch[0] * TG;
            Fy += CurlScratch[1] * TG * 0.68;
            Fz += CurlScratch[2] * TG;
        }
        break;
    }
    case 'FlowStream':
    {
        const Turn = (Source.Bearing || 0) * Radian;
        const FwdX = Math.sin(Turn), FwdZ = -Math.cos(Turn);
        const RightX = -FwdZ, RightZ = FwdX;
        const Profile = Source.StreamProfile || 'Current';

        if (Profile === 'ThermalUpdraft')
        {
            const R = Math.hypot(Dx, Dz);
            const Core = Math.max(6, Source.InnerRadius || 34);
            const Rho = R / Core;
            const Plume = Math.exp(-0.5 * Rho * Rho);
            const Radial = -((Source.Inflow || 5) * (2 * Rho / (1 + Rho * Rho))) * (Dx / Math.max(1e-3, R));
            const RadialZ = -((Source.Inflow || 5) * (2 * Rho / (1 + Rho * Rho))) * (Dz / Math.max(1e-3, R));
            Fx = Radial;
            Fy = Source.Strength * Plume;
            Fz = RadialZ;
        }
        else if (Profile === 'VesselWake')
        {
            const Along = -(Dx * FwdX + Dz * FwdZ);
            const Lat = Dx * RightX + Dz * RightZ;
            const WLen = Math.max(20, Source.HalfZ || 160);
            const HalfW = Math.max(6, Source.InnerRadius || 24);
            const Spread = 1 + 0.7 * Clamp((Along + 30) / WLen, 0, 1.5);
            const LatNorm = Lat / (HalfW * Spread);
            const Weight = Math.exp(-0.5 * LatNorm * LatNorm) * Smooth01((Along + HalfW * 2) / (HalfW * 2));
            const Roll = -LatNorm * (Source.Circulation || 8) * Weight;
            const Upwash = Math.max(0, 1 - 0.7 * LatNorm * LatNorm) * (Source.Circulation || 8) * 0.65 * Weight;
            Fx = Weight * FwdX * Source.Strength + RightX * Roll;
            Fy = Upwash;
            Fz = Weight * FwdZ * Source.Strength + RightZ * Roll;
        }
        else if (Profile === 'NavigationField')
        {
            const Lat = Dx * RightX + Dz * RightZ;
            const Core = Math.max(12, Source.InnerRadius || 36);
            const Steer = -Math.tanh(Lat / Core) * 0.55;
            Fx = (FwdX + RightX * Steer) * Source.Strength;
            Fz = (FwdZ + RightZ * Steer) * Source.Strength;
        }
        else if (Profile === 'SlipstreamJet')
        {
            const Perp = Math.hypot(Dx * RightX + Dz * RightZ, Dy);
            const Core = Math.max(6, Source.InnerRadius || 32);
            const JetGauss = Math.exp(-0.5 * Math.pow(Perp / Core, 2));
            Fx = FwdX * Source.Strength * JetGauss;
            Fz = FwdZ * Source.Strength * JetGauss;
        }
        else
        {
            const Meander = 0.16 * Math.sin((Dx * FwdX + Dz * FwdZ) * 0.015 + Time * 0.8);
            Fx = (FwdX + RightX * Meander) * Source.Strength;
            Fz = (FwdZ + RightZ * Meander) * Source.Strength;
        }
        break;
    }
    case 'Buoyancy':
    {
        const Diff = ((Source.FluidDensity ?? 1.28) - (Source.BodyDensity ?? 0.55)) / Math.max(0.1, Source.BodyDensity ?? 0.55);
        const Strat = 1 - (Source.Stratification ?? 0.35) * Clamp(Dy / Math.max(20, Source.Height), 0, 0.85);
        const R = Math.hypot(Dx, Dz);
        const Circ = R > 1e-3 ? -0.15 * Source.Strength * (Dy > 40 ? 1 : -1) * Math.exp(-R / Math.max(20, Source.Radius * 0.4)) / R : 0;
        Fx = Dx * Circ;
        Fy = Source.Strength * Clamp(Diff, -2, 3) * Strat;
        Fz = Dz * Circ;
        break;
    }
    case 'RadialBurst':
    {
        const R0 = Math.max(1, Source.InitialRadius || 8);
        const FrontR = R0 + Math.max(5, Source.WaveSpeed || 95) * Age;
        const Thick = Math.max(2, Source.Thickness || 26);
        const Dist = Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        const ShellU = (Dist - FrontR) / Thick;
        if (Math.abs(ShellU) > 3.2 || Dist < 1e-3)
        {
            Fx = Fy = Fz = 0;
        }
        else
        {
            const Decay = Math.pow(R0 / Math.max(R0, FrontR), 0.65);
            const Shell = Math.exp(-0.5 * ShellU * ShellU);
            const Mag = Source.Strength * Decay * Shell / Dist;
            Fx = Dx * Mag;
            Fy = Dy * Mag;
            Fz = Dz * Mag;
        }
        break;
    }
    case 'Attractor':
    {
        const Dist = Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        const Soft = Math.max(1, Source.SwallowRadius || 12);
        const EffDist = Math.max(Dist, Soft);
        const Ux = Dx / Math.max(1e-4, Dist), Uy = Dy / Math.max(1e-4, Dist), Uz = Dz / Math.max(1e-4, Dist);
        const TangLen = Math.hypot(Uz, Ux) || 1;
        const Tx = -Uz / TangLen, Tz = Ux / TangLen;
        const Pull = Dist < 1e-4 ? 0 : Source.Strength * (Dist / EffDist);
        Fx = -Ux * Pull + Tx * (Source.Swirl || 0);
        Fy = -Uy * Pull;
        Fz = -Uz * Pull + Tz * (Source.Swirl || 0);
        break;
    }
    case 'Repulsor':
    {
        const Dist = Math.max(1e-3, Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        const Ux = Dx / Dist, Uy = Dy / Dist, Uz = Dz / Dist;
        const TangLen = Math.hypot(Uz, Ux) || 1;
        const Tx = -Uz / TangLen, Tz = Ux / TangLen;
        Fx = Ux * Source.Strength + Tx * (Source.Swirl || 0);
        Fy = Uy * Source.Strength;
        Fz = Uz * Source.Strength + Tz * (Source.Swirl || 0);
        break;
    }
    case 'AxisAttractor':
    {
        const [Ax, Ay, Az] = AxisVectorFromAngles(Source.Bearing || 0, Source.AxisTilt || 0);
        const Proj = Clamp(Dx * Ax + Dy * Ay + Dz * Az, -(Source.AxisLength || 260) * 0.5, (Source.AxisLength || 260) * 0.5);
        const Px = Dx - Ax * Proj, Py = Dy - Ay * Proj, Pz = Dz - Az * Proj;
        const PerpDist = Math.max(1e-3, Math.sqrt(Px * Px + Py * Py + Pz * Pz));
        const Nx = Px / PerpDist, Ny = Py / PerpDist, Nz = Pz / PerpDist;
        const Sx = Ay * Nz - Az * Ny, Sy = Az * Nx - Ax * Nz, Sz = Ax * Ny - Ay * Nx;
        Fx = -Nx * Source.Strength + Ax * (Source.AxialDrive || 0) + Sx * (Source.Swirl || 0);
        Fy = -Ny * Source.Strength + Ay * (Source.AxialDrive || 0) + Sy * (Source.Swirl || 0);
        Fz = -Nz * Source.Strength + Az * (Source.AxialDrive || 0) + Sz * (Source.Swirl || 0);
        break;
    }
    case 'SplineForce':
    {
        const Turn = (Source.Bearing || 0) * Radian;
        const FwdX = Math.sin(Turn), FwdZ = -Math.cos(Turn);
        const RightX = -FwdZ, RightZ = FwdX;
        const Span = Math.max(40, Source.Radius * 1.6);
        const Along = Clamp(Dx * FwdX + Dz * FwdZ, -Span * 0.5, Span * 0.5);
        const S = Along / (Span * 0.5);
        const LateralOffset = (Source.CurveArc ?? 0.65) * 65 * Math.sin(S * Math.PI * 0.85);
        const VertOffset = (Source.HeightArc ?? 32) * (1 - S * S);
        const CurveX = Cx + FwdX * Along + RightX * LateralOffset;
        const CurveY = Cy + VertOffset;
        const CurveZ = Cz + FwdZ * Along + RightZ * LateralOffset;
        const dLatdS = (Source.CurveArc ?? 0.65) * 65 * Math.PI * 0.85 * Math.cos(S * Math.PI * 0.85) / (Span * 0.5);
        const dVertdS = (Source.HeightArc ?? 32) * (-2 * S) / (Span * 0.5);
        const TanX = FwdX + RightX * dLatdS, TanY = dVertdS, TanZ = FwdZ + RightZ * dLatdS;
        const TanLen = Math.hypot(TanX, TanY, TanZ) || 1;
        const Rx = CurveX - X, Ry = CurveY - Y, Rz = CurveZ - Z;
        const PullScale = (Source.CenterPull || 14) / Math.max(12, Source.TubeRadius || 68);
        Fx = (TanX / TanLen) * Source.Strength + Rx * PullScale;
        Fy = (TanY / TanLen) * Source.Strength + Ry * PullScale;
        Fz = (TanZ / TanLen) * Source.Strength + Rz * PullScale;
        break;
    }
    case 'SurfaceAdhesion':
    {
        const TargetH = Source.TargetOffset ?? 14;
        const DeltaH = Y - TargetH;
        const Sigma = Math.max(4, Source.BandThickness || 28);
        const Cling = Math.exp(-(DeltaH * DeltaH) / (Sigma * Sigma));
        const Turn = (Source.Bearing || 90) * Radian;
        Fx = Math.sin(Turn) * (Source.GlideDrive || 10) * Cling;
        Fy = -Source.Strength * Math.tanh(DeltaH / Sigma) * (0.4 + 0.6 * Cling);
        Fz = -Math.cos(Turn) * (Source.GlideDrive || 10) * Cling;
        break;
    }
    case 'Containment':
    {
        const Dist = Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        const Rin = Clamp(Source.InnerRatio ?? 0.58, 0.05, 0.92) * Source.Radius;
        if (Dist <= Rin || Dist < 1e-4)
        {
            Fx = Fy = Fz = 0;
        }
        else
        {
            const Wall = Clamp((Dist - Rin) / Math.max(1, Source.Radius - Rin), 0, 1);
            const Ux = Dx / Dist, Uy = Dy / Dist, Uz = Dz / Dist;
            const TangLen = Math.hypot(Uz, Ux) || 1;
            Fx = -Ux * Source.Strength * Wall + (-Uz / TangLen) * (Source.Swirl || 0) * Wall;
            Fy = -Uy * Source.Strength * Wall;
            Fz = -Uz * Source.Strength * Wall + (Ux / TangLen) * (Source.Swirl || 0) * Wall;
        }
        break;
    }
    case 'Vortex':
    {
        const R = Math.hypot(Dx, Dz);
        const Core = Math.max(3, Source.CoreRadius || 28);
        const Rho = R / Core;
        const Profile = (2 * Rho) / (1 + Rho * Rho);
        const Spin = (Source.Spin ?? 1) < 0 ? -1 : 1;
        const Vt = Source.Strength * Profile * Spin;
        const H = Math.max(20, Source.Height || 280);
        const Hs = Clamp(Dy / H, 0, 1);
        const Vr = -(Source.Inflow || 0) * (Math.exp(-Math.max(0, Dy) / (0.18 * H)) - 0.3 * Smooth01((Hs - 0.72) / 0.28)) * Profile;
        const Vy = (Source.Updraft || 0) * Math.exp(-0.42 * Rho * Rho);
        const Ux = R > 1e-4 ? Dx / R : 1, Uz = R > 1e-4 ? Dz / R : 0;
        Fx = Uz * Vt + Ux * Vr;
        Fy = Vy;
        Fz = -Ux * Vt + Uz * Vr;
        break;
    }
    case 'Torque':
    {
        const [Ax, Ay, Az] = AxisVectorFromAngles(Source.Bearing || 0, 90 - (Source.AxisTilt || 0));
        const Spin = (Source.Spin ?? 1) < 0 ? -1 : 1;
        const CxProdX = Ay * Dz - Az * Dy;
        const CxProdY = Az * Dx - Ax * Dz;
        const CxProdZ = Ax * Dy - Ay * Dx;
        const Lever = Clamp(Math.hypot(CxProdX, CxProdY, CxProdZ) / Math.max(10, Source.Radius * 0.65), 0, 1.5);
        const Norm = Math.hypot(CxProdX, CxProdY, CxProdZ) || 1;
        Fx = (CxProdX / Norm) * Source.Strength * Lever * Spin;
        Fy = (CxProdY / Norm) * Source.Strength * Lever * Spin;
        Fz = (CxProdZ / Norm) * Source.Strength * Lever * Spin;
        break;
    }
    case 'MagnusForce':
    {
        const SpinAxis = AxisVectorFromAngles(Source.Bearing || 0, 90 - (Source.AxisTilt || 0));
        const FlowDir = AxisVectorFromAngles(Source.Bearing || 90, 0);
        const W = Source.SpinRate ?? 4.5;
        const Mx = SpinAxis[1] * FlowDir[2] - SpinAxis[2] * FlowDir[1];
        const My = SpinAxis[2] * FlowDir[0] - SpinAxis[0] * FlowDir[2];
        const Mz = SpinAxis[0] * FlowDir[1] - SpinAxis[1] * FlowDir[0];
        const Scale = Source.Strength * (W / 5);
        Fx = Mx * Scale + FlowDir[0] * 2.5;
        Fy = My * Scale;
        Fz = Mz * Scale + FlowDir[2] * 2.5;
        break;
    }
    case 'Drag':
    {
        const SpeedEst = 10;
        Out[0] += (Source.Strength + (Source.QuadraticDrag || 0) * SpeedEst) * Gain;
        return { Active: true, Gate, Share, Age, Status, Mag: Source.Strength * Gain };
    }
    case 'Lift':
    {
        const AoA = (Source.AngleOfAttack ?? 12) * Radian;
        const Cl = Source.LiftCoeff ?? 1.15;
        const Head = (Source.Bearing || 65) * Radian;
        const ChordX = Math.sin(Head), ChordZ = -Math.cos(Head);
        const UpLift = Source.Strength * Cl * Math.cos(AoA);
        const ForwardInduced = Source.Strength * 0.35 * Math.sin(AoA);
        Fx = ChordX * ForwardInduced;
        Fy = UpLift;
        Fz = ChordZ * ForwardInduced;
        break;
    }
    case 'CurlNoise':
    {
        const Mode = Source.NoiseProfile || 'CurlNoise3D';
        const Scale = Math.max(6, Source.EddyScale || 52);
        const Turn = (Source.Bearing || 45) * Radian;
        const DriftX = Math.sin(Turn) * (Source.DriftSpeed || 4.5) * Time;
        const DriftZ = -Math.cos(Turn) * (Source.DriftSpeed || 4.5) * Time;
        EvalCurlPotential(X - DriftX, Y, Z - DriftZ, Scale, Source.Seed || 7331, CurlScratch);
        if (Mode === 'GustEmitter')
        {
            const Along = Dx * Math.sin(Turn) - Dz * Math.cos(Turn);
            const Burst = Math.max(0, Math.sin(2 * Math.PI * 0.25 * Time - 0.025 * Along));
            Fx = Math.sin(Turn) * Source.Strength * Burst + CurlScratch[0] * Source.Strength * 0.5;
            Fy = CurlScratch[1] * Source.Strength * 0.3;
            Fz = -Math.cos(Turn) * Source.Strength * Burst + CurlScratch[2] * Source.Strength * 0.5;
        }
        else if (Mode === 'VorticityConfinement')
        {
            EvalCurlPotential(X + 9.2 - DriftX, Y + 4.1, Z - 7.5 - DriftZ, Scale * 0.55, (Source.Seed || 7331) + 77, CurlScratch);
            const R = Math.hypot(Dx, Dz) || 1;
            Fx = CurlScratch[0] * Source.Strength * 1.15 + (-Dz / R) * Source.Strength * 0.45;
            Fy = CurlScratch[1] * Source.Strength * 0.75;
            Fz = CurlScratch[2] * Source.Strength * 1.15 + (Dx / R) * Source.Strength * 0.45;
        }
        else
        {
            Fx = CurlScratch[0] * Source.Strength;
            Fy = CurlScratch[1] * Source.Strength;
            Fz = CurlScratch[2] * Source.Strength;
        }
        break;
    }
    case 'BrownianNoise':
    {
        const Cell = Math.max(3, Source.SpatialCell || 16);
        const QuantT = Math.floor(Time * Math.max(0.5, Source.TempoHz || 4));
        const FracT = Smooth01(Time * Math.max(0.5, Source.TempoHz || 4) - QuantT);
        const SampleN = (TVal) => [
            Noise3D(X / Cell, Y / Cell, Z / Cell + TVal * 1.73, (Source.Seed || 4099) + 1),
            Noise3D(X / Cell + 19.3, Y / Cell, Z / Cell + TVal * 1.73, (Source.Seed || 4099) + 7),
            Noise3D(X / Cell, Y / Cell + 29.7, Z / Cell + TVal * 1.73, (Source.Seed || 4099) + 13)
        ];
        const A = SampleN(QuantT), B = SampleN(QuantT + 1);
        Fx = (A[0] + (B[0] - A[0]) * FracT) * Source.Strength;
        Fy = (A[1] + (B[1] - A[1]) * FracT) * Source.Strength;
        Fz = (A[2] + (B[2] - A[2]) * FracT) * Source.Strength;
        break;
    }
    case 'LorentzField':
    {
        const [Bx, By, Bz] = AxisVectorFromAngles(Source.Bearing || 30, Source.AxisTilt ?? 65);
        const Qm = Source.ChargeMassRatio ?? 1.4;
        const BScale = Source.MagneticStrength ?? 1.0;
        const EScale = Source.ElectricBias ?? 0.35;
        const Vx = -Dz * 0.08 + RefFlow[0], Vy = Dy * 0.04, Vz = Dx * 0.08;
        const CrossX = Vy * Bz - Vz * By;
        const CrossY = Vz * Bx - Vx * Bz;
        const CrossZ = Vx * By - Vy * Bx;
        const Norm = Math.hypot(CrossX, CrossY, CrossZ) || 1;
        Fx = Qm * Source.Strength * ((CrossX / Norm) * BScale + Bx * EScale);
        Fy = Qm * Source.Strength * ((CrossY / Norm) * BScale + By * EScale);
        Fz = Qm * Source.Strength * ((CrossZ / Norm) * BScale + Bz * EScale);
        break;
    }
    case 'MagneticDipole':
    {
        const [Mx, My, Mz] = AxisVectorFromAngles(Source.Bearing || 0, Source.AxisTilt ?? 80);
        const R = Math.max(Source.CoreSoftening || 26, Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
        const Dist = Math.sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
        const Rx = Dist > 1e-4 ? Dx / Dist : 0;
        const Ry = Dist > 1e-4 ? Dy / Dist : 1;
        const Rz = Dist > 1e-4 ? Dz / Dist : 0;
        const MDotR = Mx * Rx + My * Ry + Mz * Rz;
        const Factor = Math.pow((Source.CoreSoftening || 26) / R, 1.85);
        const Bx = 3 * MDotR * Rx - Mx;
        const By = 3 * MDotR * Ry - My;
        const Bz = 3 * MDotR * Rz - Mz;
        const BLen = Math.hypot(Bx, By, Bz) || 1;
        Fx = (Bx / BLen) * Source.Strength * Factor;
        Fy = (By / BLen) * Source.Strength * Factor;
        Fz = (Bz / BLen) * Source.Strength * Factor;
        break;
    }
    }

    Out[0] += Fx * Gain;
    Out[1] += Fy * Gain;
    Out[2] += Fz * Gain;
    const Mag = Math.hypot(Fx, Fy, Fz) * Gain;
    return { Active: true, Gate, Share, Age, Status, Mag };
}

export function EvaluateComposite(Scene, X, Y, Z, Time, Options = {})
{
    const Velocity = [0, 0, 0];
    const Acceleration = [0, 0, 0];
    let Damping = 0;
    const Solo = Options.Solo || null;
    const Track = Options.IncludeBreakdown ? [] : null;
    const Scratch = [0, 0, 0];

    for (const Source of Scene.Sources)
    {
        if (!Source.Enabled) continue;
        if (Solo && Source.Token !== Solo) continue;
        Scratch[0] = Scratch[1] = Scratch[2] = 0;
        const Info = EvaluateSource3D(Source, X, Y, Z, Time, Scratch);
        if (!Info.Active) continue;
        let EffChannel = Source.Channel;
        if (Source.UseFormula)
        {
            const Comp = EnsureCompiledFormula(Source);
            if (Comp?.Ok) EffChannel = Comp.OutputChannel === 'Velocity' ? 'Flow' : 'Accelerate';
        }
        if (EffChannel === 'Damp')
        {
            Damping += Scratch[0];
            if (Track) Track.push({ Source, Channel: 'Damp', Vec: [Scratch[0], 0, 0], Mag: Scratch[0], Gate: Info.Gate, Share: Info.Share });
        }
        else if (EffChannel === 'Flow')
        {
            Velocity[0] += Scratch[0]; Velocity[1] += Scratch[1]; Velocity[2] += Scratch[2];
            if (Track) Track.push({ Source, Channel: 'Flow', Vec: [...Scratch], Mag: Info.Mag, Gate: Info.Gate, Share: Info.Share });
        }
        else
        {
            Acceleration[0] += Scratch[0]; Acceleration[1] += Scratch[1]; Acceleration[2] += Scratch[2];
            if (Track) Track.push({ Source, Channel: 'Accelerate', Vec: [...Scratch], Mag: Info.Mag, Gate: Info.Gate, Share: Info.Share });
        }
    }

    const DampAtten = Math.exp(-Damping * 0.15);
    const TotalVec = [
        Velocity[0] * DampAtten + Acceleration[0] * 0.65,
        Velocity[1] * DampAtten + Acceleration[1] * 0.65,
        Velocity[2] * DampAtten + Acceleration[2] * 0.65
    ];
    const Speed = Math.hypot(Velocity[0], Velocity[1], Velocity[2]) * DampAtten;
    const AccelMag = Math.hypot(Acceleration[0], Acceleration[1], Acceleration[2]);
    const TotalMag = Math.hypot(TotalVec[0], TotalVec[1], TotalVec[2]);
    const DynamicPressurePa = 0.5 * 1.225 * Speed * Speed;

    return { Velocity, Acceleration, Damping, TotalVec, Speed, AccelMag, TotalMag, DynamicPressurePa, Breakdown: Track };
}

//------------------------------------------------------------------------------------------------------------------------
//                                  DIFFERENTIAL OPERATORS (∇·F, ∇×F) & 3D RK4 STREAMLINES
//------------------------------------------------------------------------------------------------------------------------

export function SampleDifferential(Scene, X, Y, Z, Time, Step = 4.0, Options = {})
{
    const H = Math.max(0.25, Step);
    const Inv2H = 1 / (2 * H);
    const XP = EvaluateComposite(Scene, X + H, Y, Z, Time, Options).TotalVec;
    const XM = EvaluateComposite(Scene, X - H, Y, Z, Time, Options).TotalVec;
    const YP = EvaluateComposite(Scene, X, Y + H, Z, Time, Options).TotalVec;
    const YM = EvaluateComposite(Scene, X, Y - H, Z, Time, Options).TotalVec;
    const ZP = EvaluateComposite(Scene, X, Y, Z + H, Time, Options).TotalVec;
    const ZM = EvaluateComposite(Scene, X, Y, Z - H, Time, Options).TotalVec;

    const dFx_dx = (XP[0] - XM[0]) * Inv2H;
    const dFy_dy = (YP[1] - YM[1]) * Inv2H;
    const dFz_dz = (ZP[2] - ZM[2]) * Inv2H;

    const dFz_dy = (YP[2] - YM[2]) * Inv2H;
    const dFy_dz = (ZP[1] - ZM[1]) * Inv2H;
    const dFx_dz = (ZP[0] - ZM[0]) * Inv2H;
    const dFz_dx = (XP[2] - XM[2]) * Inv2H;
    const dFy_dx = (XP[1] - XM[1]) * Inv2H;
    const dFx_dy = (YP[0] - YM[0]) * Inv2H;

    const Divergence = dFx_dx + dFy_dy + dFz_dz;
    const Curl = [dFz_dy - dFy_dz, dFx_dz - dFz_dx, dFy_dx - dFx_dy];
    const CurlMag = Math.hypot(Curl[0], Curl[1], Curl[2]);
    return { Divergence, Curl, CurlMag };
}

export function IntegrateStreamline3D(Scene, SeedX, SeedY, SeedZ, Time, Steps = 30, StepMetres = 12, Options = {})
{
    const Points = [];
    let X = SeedX, Y = SeedY, Z = SeedZ;

    const SampleUnit = (Px, Py, Pz) => {
        const Res = EvaluateComposite(Scene, Px, Py, Pz, Time, Options);
        const V = Res.TotalVec;
        const M = Res.TotalMag;
        return M > 0.05 ? [V[0] / M, V[1] / M, V[2] / M, M] : [0, 0, 0, 0];
    };

    for (let Step = 0; Step <= Steps; ++Step)
    {
        const K1 = SampleUnit(X, Y, Z);
        if (K1[3] <= 0.05) break;
        Points.push({ X, Y, Z, Mag: K1[3] });
        if (Step === Steps) break;

        const Half = StepMetres * 0.5;
        const K2 = SampleUnit(X + K1[0] * Half, Y + K1[1] * Half, Z + K1[2] * Half);
        const K3 = SampleUnit(X + K2[0] * Half, Y + K2[1] * Half, Z + K2[2] * Half);
        const K4 = SampleUnit(X + K3[0] * StepMetres, Y + K3[1] * StepMetres, Z + K3[2] * StepMetres);

        const Dx = (K1[0] + 2 * K2[0] + 2 * K3[0] + K4[0]) / 6;
        const Dy = (K1[1] + 2 * K2[1] + 2 * K3[1] + K4[1]) / 6;
        const Dz = (K1[2] + 2 * K2[2] + 2 * K3[2] + K4[2]) / 6;
        const NextX = X + Dx * StepMetres;
        const NextY = Math.max(-35, Y + Dy * StepMetres);
        const NextZ = Z + Dz * StepMetres;
        if (Math.abs(NextX) > 950 || Math.abs(NextZ) > 950 || NextY > 420) break;
        X = NextX; Y = NextY; Z = NextZ;
    }
    return Points;
}

export function IntegrateStreamlineBidirectional(Scene, SeedX, SeedY, SeedZ, Time, HalfSteps = 24, StepMetres = 10, Options = {})
{
    const Solo = Options.Solo || Scene.Viz?.SoloToken || null;
    const RadialCores = (Scene.Sources || []).filter((S) =>
        S.Enabled && (!Solo || Solo === S.Token) && (S.Model === 'Attractor' || S.Model === 'Repulsor' || S.Model === 'RadialBurst')
    );

    const Trace = (DirSign) => {
        const Out = [];
        let X = SeedX, Y = SeedY, Z = SeedZ;
        let PrevDx = 0, PrevDy = 0, PrevDz = 0;

        const SampleUnit = (Px, Py, Pz) => {
            const Res = EvaluateComposite(Scene, Px, Py, Pz, Time, Options);
            const V = Res.TotalVec;
            const M = Res.TotalMag;
            return M > 0.08 ? [V[0] / M, V[1] / M, V[2] / M, M] : [0, 0, 0, 0];
        };

        for (let Step = 0; Step < HalfSteps; ++Step)
        {
            const K1 = SampleUnit(X, Y, Z);
            if (K1[3] <= 0.08) break;
            const Ds = DirSign * StepMetres;
            const Half = Ds * 0.5;
            const K2 = SampleUnit(X + K1[0] * Half, Y + K1[1] * Half, Z + K1[2] * Half);
            if (K2[3] <= 0.08) break;
            const K3 = SampleUnit(X + K2[0] * Half, Y + K2[1] * Half, Z + K2[2] * Half);
            const K4 = SampleUnit(X + K3[0] * Ds, Y + K3[1] * Ds, Z + K3[2] * Ds);

            const Dx = (K1[0] + 2 * K2[0] + 2 * K3[0] + K4[0]) / 6;
            const Dy = (K1[1] + 2 * K2[1] + 2 * K3[1] + K4[1]) / 6;
            const Dz = (K1[2] + 2 * K2[2] + 2 * K3[2] + K4[2]) / 6;
            const DLen = Math.hypot(Dx, Dy, Dz);
            if (DLen < 0.25) break; // tight sink/source oscillation stop
            if (Step > 2 && (Dx * PrevDx + Dy * PrevDy + Dz * PrevDz) < -0.35) break; // reversed direction at singularity
            PrevDx = Dx; PrevDy = Dy; PrevDz = Dz;

            const NextX = X + Dx * Ds;
            const NextY = Math.max(-12, Y + Dy * Ds);
            const NextZ = Z + Dz * Ds;
            if (Math.abs(NextX) > 1200 || Math.abs(NextZ) > 1200 || NextY > 420) break;

            // Stop cleanly on radial attractor/repulsor cores without overshooting across center
            let HitCore = false;
            for (const Src of RadialCores)
            {
                const Rx = Src.Centre[0] - X, Ry = Src.Centre[1] - Y, Rz = Src.Centre[2] - Z;
                const RDist = Math.hypot(Rx, Ry, Rz);
                const CoreStop = Math.max(5.5, (Src.SwallowRadius || 12) * 0.48);
                if (RDist <= CoreStop) { HitCore = true; break; }
                const StepX = NextX - X, StepY = NextY - Y, StepZ = NextZ - Z;
                const StepL = Math.hypot(StepX, StepY, StepZ) || 1;
                const Proj = (Rx * StepX + Ry * StepY + Rz * StepZ) / StepL;
                if (Proj > 0 && Proj <= StepL * 1.15)
                {
                    const PerpSq = Math.max(0, RDist * RDist - Proj * Proj);
                    if (PerpSq < CoreStop * CoreStop)
                    {
                        const Trim = Math.max(0, RDist - CoreStop) / Math.max(1e-4, StepL);
                        const Tx = X + StepX * Trim, Ty = Y + StepY * Trim, Tz = Z + StepZ * Trim;
                        Out.push({ X: Tx, Y: Ty, Z: Tz, Mag: EvaluateComposite(Scene, Tx, Ty, Tz, Time, Options).TotalMag });
                        HitCore = true;
                        break;
                    }
                }
            }
            if (HitCore) break;

            X = NextX; Y = NextY; Z = NextZ;
            const NextMag = EvaluateComposite(Scene, X, Y, Z, Time, Options).TotalMag;
            Out.push({ X, Y, Z, Mag: NextMag });

            // Stop smoothly if looping right back onto seed
            if (Step > 5 && Math.hypot(X - SeedX, Y - SeedY, Z - SeedZ) < StepMetres * 0.85) break;
        }
        return Out;
    };

    const SeedSample = EvaluateComposite(Scene, SeedX, SeedY, SeedZ, Time, Options);
    if (SeedSample.TotalMag <= 0.08) return [];
    const Back = Trace(-1).reverse();
    const Fwd = Trace(1);
    return [...Back, { X: SeedX, Y: SeedY, Z: SeedZ, Mag: SeedSample.TotalMag }, ...Fwd];
}

export function EvaluateRadialProfile(Source, Steps = 48)
{
    const MaxR = Source.Radius * 1.15;
    const Out = [0, 0, 0];
    const Points = [];
    for (let I = 0; I <= Steps; ++I)
    {
        const R = (I / Steps) * MaxR;
        Out[0] = Out[1] = Out[2] = 0;
        const Res = EvaluateSource3D(
            { ...Source, Duration: 'Continuous', Enabled: true },
            Source.Centre[0] + R,
            Source.Centre[1] + (Source.Reach === 'Column' ? 12 : 0),
            Source.Centre[2],
            1.0,
            Out
        );
        Points.push({ R, Share: ResolveSpatialShare(Source, Source.Centre[0] + R, Source.Centre[1] + 2, Source.Centre[2]), Mag: Res.Mag });
    }
    return { MaxR, Points };
}

//------------------------------------------------------------------------------------------------------------------------
//                                        GPU UNIFORM PACKER (24 FORCES → VEC4 ARRAYS)
//------------------------------------------------------------------------------------------------------------------------

export const MAX_GPU_FORCES = 20;

export function PackForcesForGPU(Scene)
{
    const Solo = Scene.Viz?.SoloToken || null;
    const Active = Scene.Sources.filter((S) => S.Enabled && (!Solo || Solo === S.Token)).slice(0, MAX_GPU_FORCES);

    const AttenCode = (A) => A === 'Linear' ? 1 : A === 'InverseSquare' ? 2 : A === 'None' ? 3 : 0;
    const ReachCode = (R) => R === 'Column' ? 1 : R === 'Box' ? 2 : 0;
    const DurCode = (D) => D === 'Modulated' ? 1 : D === 'Impulse' ? 2 : 0;
    const ChanCode = (C) => C === 'Accelerate' ? 1 : C === 'Damp' ? 2 : 0;
    const ProfileCode = (S) => {
        if (S.Model === 'FlowStream') return Math.max(0, FlowStreamProfiles.indexOf(S.StreamProfile || 'Current'));
        if (S.Model === 'CurlNoise') return Math.max(0, CurlNoiseProfiles.indexOf(S.NoiseProfile || 'CurlNoise3D'));
        return 0;
    };

    const A = new Float32Array(MAX_GPU_FORCES * 4); // xyz=Centre, w=Radius
    const B = new Float32Array(MAX_GPU_FORCES * 4); // x=GpuModelId, y=Strength, z=BearingRad, w=Height
    const C = new Float32Array(MAX_GPU_FORCES * 4); // x=ReachCode, y=AttenCode, z=DurCode, w=ChanCode
    const D = new Float32Array(MAX_GPU_FORCES * 4); // x=HalfX/CoreRadius/WaveSpeed, y=HalfZ/Inflow/Thickness, z=ParamA, w=ParamB
    const E = new Float32Array(MAX_GPU_FORCES * 4); // x=TriggeredAt/Seed, y=Lasts/EddyScale, z=Repeats/ModFreq, w=ProfileCode/Spin
    const CustomSlots = [];

    for (let I = 0; I < Active.length; ++I)
    {
        const S = Active[I];
        const M = ForceModelById[S.Model] || ForceModelById.LinearWind;
        const I4 = I * 4;
        A[I4] = S.Centre[0]; A[I4 + 1] = S.Centre[1]; A[I4 + 2] = S.Centre[2]; A[I4 + 3] = S.Radius || 180;
        B[I4] = M.GpuId; B[I4 + 1] = S.Strength ?? 14; B[I4 + 2] = (S.Bearing || 0) * Radian; B[I4 + 3] = S.Height || 180;
        C[I4] = ReachCode(S.Reach); C[I4 + 1] = AttenCode(S.Attenuation); C[I4 + 2] = DurCode(S.Duration); C[I4 + 3] = ChanCode(S.Channel);

        if (S.UseFormula && CustomSlots.length < 4)
        {
            const SlotIdx = CustomSlots.length;
            const Comp = EnsureCompiledFormula(S, SlotIdx);
            if (Comp?.Ok && Comp.GlslCode)
            {
                CustomSlots.push({ Slot: SlotIdx, GlslCode: Comp.GlslCode, Token: S.Token });
                B[I4] = 19 + SlotIdx;
                C[I4 + 3] = ChanCode(Comp.OutputChannel === 'Velocity' ? 'Flow' : 'Accelerate');
                D[I4] = S.HalfX ?? 120; D[I4 + 1] = S.HalfZ ?? 120;
                D[I4 + 2] = S.Swirl ?? 16; D[I4 + 3] = S.Duration === 'Modulated' ? (S.ModFrequency ?? 0.25) : 0;
                E[I4] = S.TriggeredAt ?? 0; E[I4 + 1] = S.Lasts ?? 6; E[I4 + 2] = S.Repeats ?? 12; E[I4 + 3] = 0;
                continue;
            }
        }

        // Law-specific extra parameters packed cleanly
        let D0 = S.HalfX ?? 120, D1 = S.HalfZ ?? 120, D2 = 0, D3 = 0;
        let E0 = S.TriggeredAt ?? 0, E1 = S.Lasts ?? 6, E2 = S.Repeats ?? 12, E3 = ProfileCode(S);

        if (S.Model === 'LinearWind')
        {
            D0 = S.Shear ?? 0.16; D1 = (S.Veer ?? 9) * Radian; D2 = S.GustStrength ?? 0.28; D3 = S.GustFrequency ?? 0.14;
            E3 = S.Turbulence ?? 0.34;
        }
        else if (S.Model === 'FlowStream')
        {
            D0 = S.HalfX ?? 110; D1 = S.HalfZ ?? 220; D2 = S.InnerRadius ?? 34; D3 = S.Inflow ?? 6;
            E0 = S.Circulation ?? 8;
        }
        else if (S.Model === 'Buoyancy')
        {
            D2 = ((S.FluidDensity ?? 1.28) - (S.BodyDensity ?? 0.55)) / Math.max(0.1, S.BodyDensity ?? 0.55);
            D3 = S.Stratification ?? 0.35;
        }
        else if (S.Model === 'RadialBurst')
        {
            D0 = S.WaveSpeed ?? 90; D1 = S.Thickness ?? 26; D2 = S.InitialRadius ?? 8;
        }
        else if (S.Model === 'Attractor' || S.Model === 'Repulsor')
        {
            D2 = S.SwallowRadius ?? 14; D3 = S.Swirl ?? 0;
        }
        else if (S.Model === 'AxisAttractor')
        {
            D0 = (S.AxisTilt ?? 15) * Radian; D1 = S.AxisLength ?? 260; D2 = S.AxialDrive ?? 9; D3 = S.Swirl ?? 6;
        }
        else if (S.Model === 'SplineForce')
        {
            D2 = S.CurveArc ?? 0.65; D3 = S.HeightArc ?? 32; E0 = (S.CenterPull ?? 14) / Math.max(12, S.TubeRadius ?? 68);
        }
        else if (S.Model === 'SurfaceAdhesion')
        {
            D0 = S.TargetOffset ?? 16; D1 = S.BandThickness ?? 28; D2 = S.GlideDrive ?? 10;
        }
        else if (S.Model === 'Containment')
        {
            D2 = S.InnerRatio ?? 0.58; D3 = S.Swirl ?? 4;
        }
        else if (S.Model === 'Vortex')
        {
            D0 = S.CoreRadius ?? 28; D1 = S.Inflow ?? 9; D2 = S.Updraft ?? 16; D3 = (S.Spin ?? 1) < 0 ? -1 : 1;
        }
        else if (S.Model === 'Torque' || S.Model === 'MagnusForce' || S.Model === 'LorentzField' || S.Model === 'MagneticDipole')
        {
            D0 = (S.AxisTilt ?? 65) * Radian;
            D1 = (S.Spin ?? 1) < 0 ? -1 : 1;
            D2 = S.Model === 'MagnusForce' ? (S.SpinRate ?? 5) : S.Model === 'LorentzField' ? (S.ChargeMassRatio ?? 1.4) : (S.CoreSoftening ?? 26);
            D3 = S.Model === 'LorentzField' ? (S.ElectricBias ?? 0.35) : 1.0;
        }
        else if (S.Model === 'Drag')
        {
            D2 = S.QuadraticDrag ?? 0.14;
        }
        else if (S.Model === 'Lift')
        {
            D0 = (S.AngleOfAttack ?? 12) * Radian; D2 = S.LiftCoeff ?? 1.15;
        }
        else if (S.Model === 'CurlNoise' || S.Model === 'BrownianNoise')
        {
            D0 = S.EddyScale ?? S.SpatialCell ?? 52;
            D1 = S.DriftSpeed ?? S.TempoHz ?? 4.5;
            D2 = S.Seed ?? 7331;
        }

        // Preserve modulation params cleanly when Duration === 'Modulated' in E.z/w
        if (S.Duration === 'Modulated')
        {
            D3 = S.ModFrequency ?? 0.25;
        }

        D[I4] = D0; D[I4 + 1] = D1; D[I4 + 2] = D2; D[I4 + 3] = D3;
        E[I4] = E0; E[I4 + 1] = E1; E[I4 + 2] = E2; E[I4 + 3] = E3;
    }

    return {
        Count: Active.length,
        A,
        B,
        C,
        D,
        E,
        CustomSlots,
        CustomKey: CustomSlots.map((S) => S.GlslCode).join('\n')
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 5 PROVING-GROUND SCENARIOS
//------------------------------------------------------------------------------------------------------------------------

export function GetActiveSliceAxes(Viz)
{
    const Map = Viz?.SliceAxes;
    if (Map)
    {
        const Out = ['XZ', 'XY', 'YZ'].filter((Ax) => Boolean(Map[Ax]));
        if (Out.length > 0) return Out;
    }
    return [Viz?.SliceAxis || 'XZ'];
}

export function ConstructScenario(Key = 'lab')
{
    const BaseViz = {
        Vectors: true,
        Streamlines: false,
        BokehRibbons: true,
        Heatmap: true,
        Bounds: true,
        SliceAxis: 'XZ',
        SliceAxes: { XZ: true, XY: false, YZ: false },
        SliceHeight: 16,
        VolumeLayers: 1,
        Quantity: 'Speed',
        Colormap: 'Engineering',
        ArrowSpacing: 46,
        StreamlineSpacing: 84,
        StreamColorMode: 'FieldPalette',
        StreamSingleColor: '#68dcff',
        StreamThickness: 0.62,
        StreamEmissive: 1.0,
        StreamOpacity: 0.92,
        StreamResolution: 28,
        StreamLineCount: 220,
        BokehCount: 320,
        BokehSpeed: 0.45,
        BokehThickness: 1.0,
        BokehEmissive: 1.0,
        BokehPalette: 'BokehTeal',
        QuietCutoff: 0.25,
        Opacity: 0.58,
        FixedMax: 34,
        SoloToken: null
    };

    if (Key === 'all19')
    {
        const Ring = [];
        const Cols = 5, SpacingX = 240, SpacingZ = 220;
        ForceModels.forEach((Model, Idx) => {
            const C = Idx % Cols;
            const R = Math.floor(Idx / Cols);
            const X = (C - 2) * SpacingX;
            const Z = (R - 1.5) * SpacingZ;
            const Y = Model.Reach === 'Sphere' ? 18 : 0;
            Ring.push(ConstructForce(Model.Id, {
                Name: `${String(Idx + 1).padStart(2, '0')} · ${Model.Name}`,
                Centre: [X, Y, Z],
                Radius: Math.min(110, Model.Defaults.Radius || 105),
                HalfX: 85,
                HalfZ: 85
            }));
        });
        return {
            Key: 'all19',
            Name: 'All 19 ForceModels Matrix',
            Description: 'Unified spatial calibration stage containing all 19 ForceModel laws.',
            Probe: [-480, 16, -330],
            Sources: Ring,
            Viz: { ...BaseViz, ArrowSpacing: 52, StreamlineSpacing: 92, FixedMax: 38 },
            Lattice: { Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32 }
        };
    }

    if (Key === 'maritime')
    {
        return {
            Key: 'maritime',
            Name: 'Maritime Wake & Ocean Current',
            Description: 'Ocean FlowStream, Buoyancy pocket, trailing VesselWake, Kelvin eddy Vortex & sea breeze.',
            Probe: [25, 12, 18],
            Sources: [
                ConstructForce('FlowStream', { Name: 'Ocean Tidal Current', StreamProfile: 'Current', Strength: 16, Bearing: 85, Centre: [-40, 0, 0], HalfX: 280, HalfZ: 420, Radius: 420 }),
                ConstructForce('FlowStream', { Name: 'Vessel Wake Slipstream', StreamProfile: 'VesselWake', Strength: 22, Circulation: 12, Bearing: 82, Centre: [110, 4, -20], HalfX: 90, HalfZ: 240, Radius: 240 }),
                ConstructForce('Vortex', { Name: 'Kelvin Eddy Shed', Strength: 26, CoreRadius: 24, Inflow: 4, Updraft: 7, Centre: [-120, 0, 85], Radius: 175 }),
                ConstructForce('Buoyancy', { Name: 'Thermal Hull Buoyancy', Strength: 14, Centre: [40, 0, -10], Radius: 160 }),
                ConstructForce('LinearWind', { Name: 'Offshore Sea Breeze', Strength: 11.4, Bearing: 210, GustStrength: 0.32, Turbulence: 0.28, Centre: [0, 0, 0], Radius: 460 }),
                ConstructForce('Drag', { Name: 'Breakwater Baffle', Strength: 1.9, Bearing: 15, Centre: [230, 0, 110], HalfX: 95, HalfZ: 140, Radius: 150 })
            ],
            Viz: { ...BaseViz },
            Lattice: { Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32 }
        };
    }

    if (Key === 'turbulence')
    {
        return {
            Key: 'turbulence',
            Name: 'Atmospheric Gusts & Thermals',
            Description: 'CurlNoise ∇×ψ, GustEmitter, Vorticity Confinement, Thermal Updraft & Brownian Drift.',
            Probe: [10, 22, -15],
            Sources: [
                ConstructForce('LinearWind', { Name: 'Prevailing Crosswind', Strength: 12.8, Bearing: 242, GustStrength: 0.38, GustFrequency: 0.22, Turbulence: 0.42, Centre: [0, 0, 0], Radius: 480 }),
                ConstructForce('CurlNoise', { Name: 'Curl Turbulence ∇×ψ', NoiseProfile: 'CurlNoise3D', Strength: 14.5, EddyScale: 48, Centre: [-130, 18, -60], Radius: 220 }),
                ConstructForce('CurlNoise', { Name: 'Atmospheric Gust Emitter', NoiseProfile: 'GustEmitter', Strength: 20, Bearing: 62, EddyScale: 58, Centre: [150, 18, 90], Radius: 210, Duration: 'Modulated', ModFrequency: 0.22 }),
                ConstructForce('CurlNoise', { Name: 'Vorticity Confinement', NoiseProfile: 'VorticityConfinement', Strength: 13, EddyScale: 38, Centre: [30, 14, -180], Radius: 175 }),
                ConstructForce('FlowStream', { Name: 'Convective Thermal', StreamProfile: 'ThermalUpdraft', Strength: 19, InnerRadius: 38, Inflow: 7, Centre: [-20, 0, 20], Radius: 190 }),
                ConstructForce('BrownianNoise', { Name: 'Micro-Turbulence Drift', Strength: 9.5, SpatialCell: 14, TempoHz: 5.5, Centre: [190, 18, -120], Radius: 150 })
            ],
            Viz: { ...BaseViz, VolumeLayers: 3 },
            Lattice: { Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32 }
        };
    }

    if (Key === 'electromagnetic')
    {
        return {
            Key: 'electromagnetic',
            Name: 'Magnetic Dipole & Orbital Core',
            Description: '3D Magnetic Dipole, Lorentz E+v×B, AxisAttractor, Hermite Spline Conduit & Containment.',
            Probe: [45, 24, 20],
            Sources: [
                ConstructForce('MagneticDipole', { Name: 'Primary Magnetic Dipole', Strength: 24, CoreSoftening: 28, AxisTilt: 82, Centre: [0, 22, 0], Radius: 230 }),
                ConstructForce('LorentzField', { Name: 'Lorentz E + v×B Ring', Strength: 18, ChargeMassRatio: 1.5, AxisTilt: 72, Centre: [-210, 22, -90], Radius: 180 }),
                ConstructForce('AxisAttractor', { Name: 'Filament Axis Trap', Strength: 17, AxialDrive: 9, Swirl: 7, Bearing: 50, Centre: [220, 18, -110], Radius: 175 }),
                ConstructForce('SplineForce', { Name: 'Hermite Guide Conduit', Strength: 20, Bearing: 35, CurveArc: 0.75, HeightArc: 42, Centre: [-180, 12, 160], Radius: 210 }),
                ConstructForce('Containment', { Name: 'Toroidal Containment Shell', Strength: 28, InnerRatio: 0.52, Swirl: 5, Centre: [190, 20, 150], Radius: 190 }),
                ConstructForce('Torque', { Name: 'Angular Torque Couple', Strength: 22, AxisTilt: 15, Centre: [0, 20, -240], Radius: 150 }),
                ConstructForce('SurfaceAdhesion', { Name: 'Boundary Adhesion Foil', Strength: 16, TargetOffset: 16, GlideDrive: 12, Centre: [0, 0, 240], Radius: 165 })
            ],
            Viz: { ...BaseViz, VolumeLayers: 3 },
            Lattice: { Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32 }
        };
    }

    return {
        Key: 'lab',
        Name: 'Wind & Vortex Lab',
        Description: 'LinearWind + Burgers-Rott Vortex + Slipstream Jet + Impulse Shockwave + Attractor/Repulsor + CurlNoise + Drag.',
        Probe: [-95, 16, 42],
        Sources: [
            ConstructForce('LinearWind', { Name: 'Prevailing Linear Wind', Strength: 13.8, Bearing: 228, GustStrength: 0.28, GustFrequency: 0.16, Turbulence: 0.34, Centre: [-20, 0, 0], Radius: 440 }),
            ConstructForce('Vortex', {
                Name: 'Cyclonic Vortex Column',
                Strength: 36,
                CoreRadius: 28,
                Inflow: 10,
                Updraft: 18,
                Centre: [20, 0, -40],
                Radius: 215,
                Motion: {
                    Mode: 'SplineCurve',
                    Loop: 'PingPong',
                    SpeedMps: 32,
                    Phase: 0,
                    Waypoints: [[20, 0, -40], [-65, 22, 30], [90, 0, 85]]
                }
            }),
            ConstructForce('FlowStream', { Name: 'Venturi Slipstream Jet', StreamProfile: 'SlipstreamJet', Strength: 24, Bearing: 88, InnerRadius: 30, Centre: [-210, 32, -165], HalfX: 75, HalfZ: 180, Radius: 200 }),
            ConstructForce('RadialBurst', { Name: 'Impulse Shockwave Shell', Strength: 44, WaveSpeed: 88, Thickness: 24, Radius: 230, Centre: [-80, 38, 185], Duration: 'Impulse', Repeats: 10, Lasts: 5.5 }),
            ConstructForce('Attractor', { Name: 'Singularity Attractor', Strength: 24, Swirl: 0, Centre: [240, 48, 95], Radius: 195 }),
            ConstructForce('Repulsor', { Name: 'Deflector Repulsor', Strength: 24, Swirl: 0, Centre: [160, 44, -180], Radius: 175 }),
            ConstructForce('CurlNoise', { Name: 'Curl Turbulence Pocket', NoiseProfile: 'CurlNoise3D', Strength: 12, EddyScale: 44, Centre: [-240, 42, 40], Radius: 175 }),
            ConstructForce('Drag', { Name: 'Damping Baffle Box', Strength: 1.65, Bearing: 12, Centre: [110, 0, 195], HalfX: 85, HalfZ: 75, Radius: 115 })
        ],
        Viz: { ...BaseViz },
        Lattice: { Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32 }
    };
}

export function ResolveCascadeBudget(Scene)
{
    const L = Scene.Lattice || { Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32 };
    const BytesPerCascade = L.CellsPerSide * L.CellsPerSide * L.VerticalCells * 8;
    const Counts = { VectorField: 0, FlowField: 0, TurbulenceField: 0, Active: 0, Impulse: 0 };
    for (const S of Scene.Sources)
    {
        if (Counts[S.System] !== undefined) Counts[S.System]++;
        if (S.Enabled) Counts.Active++;
        if (S.Duration === 'Impulse') Counts.Impulse++;
    }
    return {
        Counts,
        BytesPerCascade,
        TotalBytes: BytesPerCascade * L.Cascades,
        Tiers: Array.from({ length: L.Cascades }, (_, Tier) => {
            const Cell = L.FinestCell * Math.pow(4, Tier);
            return { Tier, CellMetres: Cell, SpanMetres: Cell * L.CellsPerSide };
        })
    };
}

export function EncodeSimulationSpaceToml(Scene)
{
    const Lines = [
        '[VectorFieldWorkspace]',
        `Scenario = "${Scene.Name}"`,
        `ProbeMetres = [${Scene.Probe.map((V) => Number(V).toFixed(2)).join(', ')}]`,
        `SourceCount = ${Scene.Sources.length}`
    ];
    for (const S of Scene.Sources)
    {
        Lines.push(
            '',
            '[[ForceSources]]',
            `Token = "${S.Token}"`,
            `Name = "${S.Name}"`,
            `System = "${S.System}"`,
            `ForceModel = "${S.Model}"`,
            `ForceDomain = "${S.Domain}"`,
            `ForceAttenuation = "${S.Attenuation}"`,
            `EForceDuration = "${S.Duration}"`,
            `Enabled = ${S.Enabled}`,
            `CentreMetres = [${S.Centre.map((V) => Number(V).toFixed(2)).join(', ')}]`,
            `Reach = "${S.Reach}"`,
            `RadiusMetres = ${Number(S.Radius).toFixed(2)}`,
            `Strength = ${Number(S.Strength).toFixed(3)}`
        );
        if (S.Motion && S.Motion.Mode !== 'None')
        {
            Lines.push(
                `MotionMode = "${S.Motion.Mode}"`,
                `MotionLoop = "${S.Motion.Loop}"`,
                `MotionSpeedMps = ${Number(S.Motion.SpeedMps || 40).toFixed(2)}`,
                `MotionPhase = ${Number(S.Motion.Phase || 0).toFixed(3)}`,
                `MotionWaypoints = [${(S.Motion.Waypoints || []).map((P) => `[${P.map((V) => Number(V).toFixed(2)).join(', ')}]`).join(', ')}]`
            );
        }
    }
    return Lines;
}
