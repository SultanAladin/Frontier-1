/*============================================================================================================================================
                                                     FORCE FIELD CATALOG
============================================================================================================================================*/
// 🧩 Canonical force-generator definitions: the 19 field-law descriptors plus the compiled custom-formula field, each carrying its parameter schema,
// glyph, governing equation and the defaults that seed a new source. The inspector, outliner, diagrams and evaluator all read this one catalog.

export const ForceDomains = Object.freeze({
  VectorField: { Id: 'VectorField', Label: 'VectorField', Subtitle: 'Analytical directional, radial, orbital and electromagnetic laws', Color: '#4aa8ff' },
  ChannelField: { Id: 'ChannelField', Label: 'ChannelField', Subtitle: 'Directed currents, spline corridors and boundary adhesion', Color: '#3ad69c' },
  TurbulenceField: { Id: 'TurbulenceField', Label: 'TurbulenceField', Subtitle: 'Divergence-free curl noise, gusting and micro-drift', Color: '#e6a23c' },
});

export const EnvelopeKinds = Object.freeze(['Sphere', 'Column', 'Box', 'Infinite']);
export const AttenuationKinds = Object.freeze(['SmoothStep', 'Linear', 'InverseSquare', 'Constant']);
export const DurationKinds = Object.freeze(['Continuous', 'Modulated', 'Impulse']);
export const MotionKinds = Object.freeze(['Static', 'LinearAB', 'SplineCurve', 'CircularOrbit', 'Figure8']);
export const LoopPolicies = Object.freeze(['PingPong', 'Loop', 'Once']);
export const EvaluatedQuantities = Object.freeze([
  { Id: 'Speed', Label: 'Speed', Unit: 'm/s', Note: 'Advection velocity magnitude |v|' },
  { Id: 'Acceleration', Label: 'Acceleration', Unit: 'm/s²', Note: 'Direct acceleration magnitude |a|' },
  { Id: 'Curl', Label: 'Curl', Unit: '1/s', Note: 'Vorticity magnitude |∇ × v|' },
  { Id: 'Divergence', Label: 'Divergence', Unit: '1/s', Note: 'Compression rate ∇ · v' },
  { Id: 'Influence', Label: 'Influence', Unit: '-', Note: 'Bounded per-source attenuation sum' },
]);

const VectorGlyph = (body) => `<svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round">${body}</svg>`;

// Parameter schema fields: { Key, Label, Unit, Min, Max, Step, Default, Hint, Precision }
const P = (Key, Label, Min, Max, Step, Default, Unit = '', Hint = '') => ({ Key, Label, Min, Max, Step, Default, Unit, Hint });
const Strength = (unit, max, def, hint) => P('Strength', 'Magnitude', 0, max, max / 200, def, unit, hint);

export const FORCE_LAWS = Object.freeze([
  {
    Ordinal: 0,
    Id: 'LinearWind',
    Name: 'Linear Wind',
    Domain: 'VectorField',
    Channel: 'velocity',
    Unit: 'm/s',
    Summary: 'A prevailing breeze that strengthens and turns as it rises, with gusts riding on the mean.',
    Equation: 'v = S · (1 + s_z h) · d(θ + v_z h) + ĝ sin(2π f t)',
    Envelope: 'Column',
    Attenuation: 'Constant',
    Radius: 9,
    Height: 8,
    Glyph: VectorGlyph('<path d="M2.5 7h9.5a2.2 2.2 0 1 0-2.1-2.8"/><path d="M2.5 12h12.5a2 2 0 1 1-1.9 2.6"/><path d="M2.5 17h7.5a1.8 1.8 0 1 1-1.7 2.4"/>'),
    Params: [
      Strength('m/s', 40, 6.5, 'Mean wind speed at the base of the column.'),
      P('Shear', 'Vertical shear', -0.4, 0.6, 0.01, 0.08, '/m', 'Speed gained per metre of height. Negative values weaken the wind aloft.'),
      P('Veer', 'Directional veer', -30, 30, 0.2, 1.6, '°/m', 'Directional turn per metre of height (the Ekman spiral effect).'),
      P('GustAmplitude', 'Gust amplitude', 0, 1, 0.01, 0.28, '[-]', 'Fraction of the mean speed carried by the periodic gust.'),
      P('GustFrequency', 'Gust frequency', 0.02, 2, 0.01, 0.22, 'Hz', 'How often a gust passes.'),
      P('Turbulence', 'Turbulence', 0, 1, 0.01, 0.18, '[-]', 'Curl-noise roughness added on top of the mean direction.'),
    ],
  },
  {
    Ordinal: 1,
    Id: 'DirectedStream',
    Name: 'Directed Stream',
    Domain: 'ChannelField',
    Channel: 'velocity',
    Unit: 'm/s',
    Summary: 'A confined current in an oriented corridor, spreading as it leaves the nozzle and accelerating through a narrowing throat.',
    Equation: 'v = S · (1 + κ (1 - ρ)) · (1 - σ ρ²) · d̂ · A(u)',
    Envelope: 'Box',
    Attenuation: 'SmoothStep',
    HalfExtents: [10, 3.2, 2.4],
    Glyph: VectorGlyph('<path d="M2.2 8.2c3.4-3 6.2 3 9.6 0l3.4-3"/><path d="M2.2 12.6c3.4-3 6.2 3 9.6 0l3.4-3"/><path d="M2.2 17c3.4-3 6.2 3 9.6 0l3.4-3"/><path d="M16.4 3.6 21.6 5.2l-2 4.6"/>'),
    Params: [
      Strength('m/s', 60, 9, 'Mean current speed along the corridor axis.'),
      P('Spread', 'Edge spread', 0, 1, 0.01, 0.35, '[-]', 'How much the current slows toward the corridor walls.'),
      P('Throat', 'Throat gain', 0, 2, 0.01, 0.55, '[-]', 'Venturi acceleration where the corridor narrows near its anchor.'),
      P('Turbulence', 'Stream turbulence', 0, 1, 0.01, 0.12, '[-]', 'Cross-stream wander added to the current.'),
    ],
  },
  {
    Ordinal: 2,
    Id: 'Buoyancy',
    Name: 'Buoyancy',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'The upward push a body feels when it is lighter than the air around it, weakening with height.',
    Equation: 'a_z = g (ρ_amb/ρ_body − 1) · A(u)',
    Envelope: 'Column',
    Attenuation: 'Linear',
    Radius: 6,
    Height: 9,
    Glyph: VectorGlyph('<path d="M12 20V4.5"/><path d="M8.2 8.6 12 4.5l3.8 4.1"/><circle cx="12" cy="14.4" r="2.1"/>'),
    Params: [
      Strength('m/s²', 30, 3.2, 'Upward acceleration at the anchor.'),
      P('Gravity', 'Gravity', 0, 20, 0.01, 9.81, 'm/s²', 'Local gravitational acceleration.'),
      P('DensityRatio', 'Ambient / body density', 0.1, 3, 0.01, 1.6, '[-]', 'Ratio driving the net lift; above 1 the body rises.'),
      P('HeightFalloff', 'Height falloff', 0, 1, 0.01, 0.35, '[-]', 'How quickly the lift fades toward the top of the column.'),
    ],
  },
  {
    Ordinal: 3,
    Id: 'RadialBurst',
    Name: 'Radial Burst',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'An expanding spherical wavefront: a shell of outward acceleration that sweeps past a probe once per impulse.',
    Equation: 'a = S · (R₀/R)^κ · e^(−(r−R)²/2σ²) · r̂',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 11,
    Duration: 'Impulse',
    Glyph: VectorGlyph('<circle cx="12" cy="12" r="2"/><circle cx="12" cy="12" r="5.2" stroke-dasharray="2 2"/><path d="M12 1.8v2.4M12 19.8v2.4M1.8 12h2.4M19.8 12h2.4M5 5l1.7 1.7M17.3 17.3 19 19M19 5l-1.7 1.7M6.7 17.3 5 19"/>'),
    Params: [
      Strength('m/s²', 80, 18, 'Peak outward acceleration at the wavefront.'),
      P('WaveSpeed', 'Wave speed', 0.5, 40, 0.1, 11, 'm/s', 'How fast the shell expands.'),
      P('ShellWidth', 'Shell width σ', 0.1, 6, 0.05, 1.1, 'm', 'Thickness of the accelerating shell.'),
      P('StartRadius', 'Start radius', 0.05, 4, 0.05, 0.6, 'm', 'Radius at the moment the impulse fires.'),
      P('Decay', 'Radius decay κ', 0, 2, 0.01, 0.65, '[-]', 'Fall-off of peak strength as the shell grows.'),
    ],
  },
  {
    Ordinal: 4,
    Id: 'Attractor',
    Name: 'Attractor',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A gravitational well: inward pull with a soft core and an optional tangential swirl that keeps bodies orbiting.',
    Equation: 'a = −S · r̂ / (1 + (r/r_c)²) + ω (ẑ × r̂) · A(u)',
    Envelope: 'Sphere',
    Attenuation: 'InverseSquare',
    Radius: 9,
    Glyph: VectorGlyph('<circle cx="12" cy="12" r="1.8"/><path d="M2.6 2.6 6 6M21.4 2.6 18 6M2.6 21.4 6 18M21.4 21.4 18 18"/><circle cx="12" cy="12" r="6.4" stroke-dasharray="1.8 2.4"/>'),
    Params: [
      Strength('m/s²', 60, 14, 'Inward acceleration at the soft core.'),
      P('CoreRadius', 'Soft core', 0.2, 8, 0.05, 1.6, 'm', 'Radius inside which the pull stops growing, preventing a singularity.'),
      P('Swirl', 'Tangential swirl', 0, 20, 0.05, 4.5, 'm/s²', 'Sideways push that turns a fall into an orbit.'),
      P('SpinBias', 'Vertical bias', -5, 5, 0.05, 0, 'm/s²', 'Upward or downward bias applied with the pull.'),
    ],
  },
  {
    Ordinal: 5,
    Id: 'Repulsor',
    Name: 'Repulsor',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A pressure dome: steady outward acceleration with a hard, sharp-edged core.',
    Equation: 'a = +S · r̂ / (1 + (r/r_c)^p) · A(u)',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 8,
    Glyph: VectorGlyph('<circle cx="12" cy="12" r="1.8"/><path d="M5.4 5.4 2.4 2.4M18.6 5.4l3-3M5.4 18.6l-3 3M18.6 18.6l3 3"/><path d="M2.4 5.4V2.4h3M21.6 5.4V2.4h-3M2.4 18.6v3h3M21.6 18.6v3h-3"/>'),
    Params: [
      Strength('m/s²', 60, 12, 'Outward acceleration at the core.'),
      P('CoreRadius', 'Soft core', 0.2, 8, 0.05, 1.2, 'm', 'Radius inside which the push is capped.'),
      P('Sharpness', 'Falloff power', 1, 6, 0.05, 2.4, '[-]', 'How abruptly the dome drops to zero at its edge.'),
      P('Lift', 'Vertical bias', -10, 10, 0.05, 0.6, 'm/s²', 'Small upward bias for dusty ejecta.'),
    ],
  },
  {
    Ordinal: 6,
    Id: 'AxisAttractor',
    Name: 'Axis Attractor',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A capsule-shaped well that pulls toward the nearest point of a straight segment and slides along it.',
    Equation: 'a = −S · A(u⊥) · q̂ + S_ax · axiŝ,  q = closest(p, A, B)',
    Envelope: 'Column',
    Attenuation: 'InverseSquare',
    Radius: 6,
    Height: 12,
    Glyph: VectorGlyph('<path d="M4.6 19.4 19.4 4.6"/><path d="M8 8.4 10.6 11M13.4 13l2.6 2.6"/><circle cx="12" cy="12" r="1.4"/>'),
    Params: [
      Strength('m/s²', 50, 8.5, 'Radial pull toward the axis segment.'),
      P('AxisLength', 'Axis length', 0.5, 24, 0.1, 12, 'm', 'Length of the attracting segment from the anchor.'),
      P('CoreRadius', 'Core radius', 0.2, 6, 0.05, 1.4, 'm', 'Soft radius of the radial pull.'),
      P('AxialPull', 'Axial pull', -8, 8, 0.05, 1.2, 'm/s²', 'Acceleration along the axis toward its far end.'),
    ],
  },
  {
    Ordinal: 7,
    Id: 'SplineForce',
    Name: 'Spline Force',
    Domain: 'ChannelField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A bowed guide curve: bodies are pulled onto the spline and pushed along it, the path a cable or tow line would impose.',
    Equation: 'a = S_rad · (C(s*) − p) + S_tan · T̂(s*)',
    Envelope: 'Box',
    Attenuation: 'SmoothStep',
    HalfExtents: [9, 4.5, 3.5],
    Glyph: VectorGlyph('<path d="M2.6 20.4c4-4 4-12.8 8-12.8s4 8.8 8.8 8.8"/><path d="M16.8 13.6 19.4 16.4l-2.6 2.6"/>'),
    Params: [
      Strength('m/s²', 40, 6, 'Pull toward the spline curve.'),
      P('PathLength', 'Path length', 1, 24, 0.1, 11, 'm', 'Distance the curve spans along the source direction.'),
      P('Bow', 'Curve bow', -1, 1, 0.01, 0.32, '[-]', 'Sideways bow of the guide curve.'),
      P('TangentialPush', 'Tangential push', 0, 20, 0.05, 3.4, 'm/s²', 'Acceleration along the tangent of the curve.'),
      P('TubeRadius', 'Capture radius', 0.2, 8, 0.05, 2.6, 'm', 'Distance at which the curve starts to pull bodies in.'),
    ],
  },
  {
    Ordinal: 8,
    Id: 'BoundaryAdhesion',
    Name: 'Boundary Adhesion',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'Force that clings bodies to a distance just short of the envelope wall, the way spray wets a windscreen.',
    Equation: 'a = −S · sign(φ − d_t) · e^(−(φ−d_t)²/σ²) · n̂',
    Envelope: 'Sphere',
    Attenuation: 'Constant',
    Radius: 7,
    Glyph: VectorGlyph('<path d="M2.4 20.4h19.2"/><path d="M4.6 12.4c2.6 4.4 8.2 4.4 10.8 0"/><path d="M12 3.6v6.4M9.6 7.6 12 10l2.4-2.4"/>'),
    Params: [
      Strength('m/s²', 60, 16, 'Cling acceleration toward the wall.'),
      P('TargetDistance', 'Cling distance', 0, 4, 0.02, 0.35, 'm', 'Preferred stand-off from the envelope wall.'),
      P('Softness', 'Cling softness σ', 0.05, 4, 0.02, 0.7, 'm', 'Width of the adhesion band.'),
      P('NormalBias', 'Normal bias', -5, 5, 0.05, 0, 'm/s²', 'Outward or inward bias along the wall normal.'),
    ],
  },
  {
    Ordinal: 9,
    Id: 'Containment',
    Name: 'Containment',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A soft-walled cell: nothing beyond the inner radius feels anything, and everything near the wall is pushed back in.',
    Equation: 'a = −S · (r − R_in)/(R − R_in) · r̂ − c_d (v·r̂) r̂,  r > R_in',
    Envelope: 'Sphere',
    Attenuation: 'Constant',
    Radius: 9,
    Glyph: VectorGlyph('<circle cx="12" cy="12" r="8.2"/><circle cx="12" cy="12" r="4" stroke-dasharray="1.8 2"/><path d="M12 2.8v2.4M12 18.8v2.4M2.8 12h2.4M18.8 12h2.4"/>'),
    Params: [
      Strength('m/s²', 40, 9, 'Restoring acceleration at the wall.'),
      P('InnerRatio', 'Inner radius ratio', 0.05, 0.95, 0.01, 0.55, '[-]', 'Fraction of the envelope radius that stays free of restoring force.'),
      P('WallDamping', 'Wall damping', 0, 4, 0.02, 0.8, '1/s', 'Velocity damping applied only near the wall.'),
    ],
  },
  {
    Ordinal: 10,
    Id: 'RotationalColumn',
    Name: 'Rotational Column',
    Domain: 'VectorField',
    Channel: 'velocity',
    Unit: 'm/s',
    Summary: 'A Burgers–Rott swirl: tangential speed peaks at the core radius, with gentle inflow and an updraft rising through it.',
    Equation: 'v_θ = S(1 − e^(−ρ²))/ρ,  v_r = −I ρ,  v_z = U e^(−0.42ρ²)',
    Envelope: 'Column',
    Attenuation: 'SmoothStep',
    Radius: 6,
    Height: 12,
    Glyph: VectorGlyph('<path d="M12 12a1.6 1.6 0 1 1-1.6-1.6 3.4 3.4 0 0 1 3.4 3.4A5.4 5.4 0 0 1 6.6 13.8 7.6 7.6 0 0 1 19.4 12"/>'),
    Params: [
      Strength('m/s', 50, 13, 'Peak tangential speed.'),
      P('CoreRadius', 'Core radius', 0.2, 8, 0.05, 1.5, 'm', 'Radius of the solid-body rotation core.'),
      P('Inflow', 'Radial inflow', 0, 4, 0.02, 0.22, '[-]', 'Inward suction toward the axis (ρ-scaled).'),
      P('Updraft', 'Updraft', -20, 20, 0.1, 3.2, 'm/s', 'Vertical speed on the axis, decaying with radius.'),
    ],
  },
  {
    Ordinal: 11,
    Id: 'Torque',
    Name: 'Torque',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A twisting couple about an axis: tangential acceleration that spins bodies without pulling them anywhere.',
    Equation: 'a = S · (axiŝ × r̂) · A(u)',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 6,
    Glyph: VectorGlyph('<path d="M19.4 9.6A7.6 7.6 0 1 0 17 17.4"/><path d="M17.6 5.6v4.4h-4.4"/><circle cx="12" cy="12" r="1.6"/>'),
    Params: [
      Strength('m/s²', 40, 7.5, 'Tangential acceleration applied around the axis.'),
      P('AxisTilt', 'Axis tilt', -90, 90, 0.5, 0, '°', 'Tilt of the spin axis away from +Z, toward the source direction.'),
      P('CoreRadius', 'Core radius', 0.2, 6, 0.05, 1.1, 'm', 'Soft radius where the couple builds up.'),
    ],
  },
  {
    Ordinal: 12,
    Id: 'MagnusForce',
    Name: 'Magnus Force',
    Domain: 'ChannelField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'Spin-induced curve on a moving body: the force is perpendicular to both its spin axis and its relative velocity.',
    Equation: 'a = C_M · A(u) · (ω̂ × v_rel)',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 8,
    Glyph: VectorGlyph('<circle cx="12" cy="12" r="6.4"/><path d="M8.6 7.4c3.4 1.2 5.4 4.4 5.4 9.2"/><path d="M12 12h8.6M17.8 9.4 20.6 12l-2.8 2.6"/>'),
    Params: [
      Strength('m/s²', 30, 5.5, 'Magnus gain at the core.'),
      P('SpinRate', 'Spin rate', -200, 200, 1, 38, 'rad/s', 'Rotation rate of the body about the source direction.'),
      P('CoreRadius', 'Core radius', 0.2, 6, 0.05, 1.8, 'm', 'Soft radius where the Magnus coupling is strongest.'),
    ],
  },
  {
    Ordinal: 13,
    Id: 'Drag',
    Name: 'Drag',
    Domain: 'VectorField',
    Channel: 'damping',
    Unit: '1/s',
    Summary: 'Velocity-proportional resistance: a pure damping channel that removes kinetic energy without adding any direction of its own.',
    Equation: 'a_d = −(k₁ + k₂ |v_rel|) · v_rel · A(u)',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 9,
    Glyph: VectorGlyph('<path d="M6 3.4v17.2M18 3.4v17.2M2.4 12h3.6M18 12h3.6"/><path d="M9.8 8.2h4.4M9.8 12h4.4M9.8 15.8h4.4"/>'),
    Params: [
      P('Linear', 'Linear k₁', 0, 6, 0.02, 0.9, '1/s', 'Damping proportional to speed.'),
      P('Quadratic', 'Quadratic k₂', 0, 1, 0.005, 0.06, 's/m', 'Damping proportional to speed squared.'),
      P('Strength', 'Damping scale', 0, 4, 0.01, 1, '[-]', 'Overall scale applied to both coefficients.'),
    ],
  },
  {
    Ordinal: 14,
    Id: 'Lift',
    Name: 'Lift',
    Domain: 'ChannelField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'Airfoil lift: a square-of-speed force perpendicular to the flow, tilting with the angle of attack.',
    Equation: 'a = ½ ρ C_L |v_rel|² · (ω̂ × v̂_rel) · A(u)',
    Envelope: 'Column',
    Attenuation: 'SmoothStep',
    Radius: 7,
    Height: 7,
    Glyph: VectorGlyph('<path d="M2.8 15.4c4.2 0 6.4-2.6 9.2-6.2 1.8-2.2 4.6-2 6.4-.2 2 2 1.4 5-1.2 6.4-3.6 1.8-9.4 2.4-14.4 0z"/><path d="M6 19.6h9.6M14 17l2.6 2.6L14 22"/>'),
    Params: [
      Strength('m/s²', 40, 4.2, 'Lift gain at the core.'),
      P('LiftCoefficient', 'Lift coefficient C_L', -2, 2, 0.01, 0.72, '[-]', 'Sign selects upwash or downforce.'),
      P('AirDensity', 'Air density ρ', 0.1, 5, 0.01, 1.225, 'kg/m³', 'Density of the moving medium.'),
      P('AngleOfAttack', 'Angle of attack', -40, 40, 0.5, 8, '°', 'Tilt of the lift vector away from the flow normal.'),
    ],
  },
  {
    Ordinal: 15,
    Id: 'CurlNoise',
    Name: 'Curl Noise',
    Domain: 'TurbulenceField',
    Channel: 'velocity',
    Unit: 'm/s',
    Summary: 'Divergence-free turbulence generated as the curl of a vector potential, so eddies never pile fluid up or tear holes in it.',
    Equation: 'v = S · A(u) · ∇ × ψ(p/λ − v_drift t)',
    Envelope: 'Box',
    Attenuation: 'SmoothStep',
    HalfExtents: [10, 10, 4],
    Glyph: VectorGlyph('<path d="M2.6 7.4a3 3 0 1 1 5.6 1.6 3 3 0 1 0 5.6 1"/><path d="M14.6 5.4a2.2 2.2 0 0 0-3.2-.2"/><path d="M4 16.8c2.4-2 4.8 2 7.2 0s4.8 2 7.2 0"/>'),
    Params: [
      Strength('m/s', 40, 4.5, 'Root-mean-square eddy speed.'),
      P('EddyScale', 'Eddy scale λ', 0.5, 20, 0.1, 4.6, 'm', 'Size of the largest eddies.'),
      P('Octaves', 'Octaves', 1, 4, 1, 2, '[-]', 'Detail levels summed into the potential.'),
      P('Drift', 'Advection drift', 0, 4, 0.01, 0.35, '[-]', 'How fast the turbulence envelope scrolls.'),
      P('Seed', 'Seed', 0, 64, 1, 7, '[-]', 'Deterministic noise seed; identical on every reload.'),
    ],
  },
  {
    Ordinal: 16,
    Id: 'BrownianNoise',
    Name: 'Brownian Noise',
    Domain: 'TurbulenceField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'Deterministic micro-drift: a smooth pseudo-random acceleration field standing in for unresolved collisions.',
    Equation: 'a = S · A(u) · N₃(p/λ, t · f)',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 7,
    Glyph: VectorGlyph('<path d="M2.6 17.6 6.2 10l3.4 4.6L13.4 5l3 6.6 3.2-3.4 2 2"/><circle cx="2.6" cy="17.6" r="1"/><circle cx="21.4" cy="8.2" r="1"/>'),
    Params: [
      Strength('m/s²', 20, 2.4, 'Acceleration jitter amplitude.'),
      P('CellScale', 'Cell scale λ', 0.2, 10, 0.05, 1.8, 'm', 'Correlation length of the jitter field.'),
      P('Rate', 'Evolution rate', 0, 6, 0.05, 0.9, 'Hz', 'How quickly the jitter field changes.'),
      P('Seed', 'Seed', 0, 64, 1, 23, '[-]', 'Deterministic jitter seed.'),
    ],
  },
  {
    Ordinal: 17,
    Id: 'LorentzField',
    Name: 'Lorentz Field',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'Charged-body deflection: a uniform electric push plus a magnetic force that always turns the velocity, never speeds it up.',
    Equation: 'a = (q/m) [ E⃗ + v_rel × B⃗ ] · A(u)',
    Envelope: 'Box',
    Attenuation: 'Constant',
    HalfExtents: [9, 9, 5],
    Glyph: VectorGlyph('<path d="M2.6 9.4c1.6-4.6 3.6-4.6 4.6 0s3 4.6 4.4 0 1.8-4.6 2.4 0"/><path d="M2 4h12"/><path d="M11.6 2.2 14 4l-2.4 1.8"/><circle cx="17.6" cy="16" r="2.4"/><path d="M17.6 10.4v1.2M17.6 20.4v1.2M12 16h1.2M22 16h1.2"/>'),
    Params: [
      Strength('m/s²', 40, 5, 'Overall electromagnetic gain.'),
      P('ChargeToMass', 'Charge / mass q/m', -4, 4, 0.01, 1.15, 'C/kg', 'Polarity and strength of the coupling.'),
      P('ElectricField', 'Electric field E', -10, 10, 0.05, 1.4, 'V/m', 'Uniform push along the source direction.'),
      P('MagneticField', 'Magnetic field B', -10, 10, 0.05, 2.1, 'T', 'Uniform field along +Z; curves the velocity.'),
    ],
  },
  {
    Ordinal: 18,
    Id: 'MagneticDipole',
    Name: 'Magnetic Dipole',
    Domain: 'VectorField',
    Channel: 'acceleration',
    Unit: 'm/s²',
    Summary: 'A bar-magnet field: field lines leave one pole and return to the other, with a softened singularity at the centre.',
    Equation: 'B = S · [3(m̂·r̂) r̂ − m̂] / max(r, r_soft)³ · A(u)',
    Envelope: 'Sphere',
    Attenuation: 'SmoothStep',
    Radius: 8,
    Glyph: VectorGlyph('<path d="M9.4 5.2C5.6 5.2 4 12 4 12s1.6 6.8 5.4 6.8M14.6 5.2c3.8 0 5.4 6.8 5.4 6.8s-1.6 6.8-5.4 6.8"/><rect x="9.2" y="5.2" width="5.6" height="13.6" rx="1.6"/>'),
    Params: [
      Strength('m/s²', 80, 15, 'Dipole strength at the softened core.'),
      P('DipoleMoment', 'Moment ratio', -3, 3, 0.01, 1, '[-]', 'Sign flips the poles.'),
      P('SoftRadius', 'Soft radius', 0.2, 5, 0.05, 0.9, 'm', 'Core radius that removes the r⁻³ singularity.'),
      P('Aim', 'Pole aim', -90, 90, 0.5, 0, '°', 'Tilt of the dipole moment away from +Z.'),
    ],
  },
  {
    Ordinal: 19,
    Id: 'CustomFormula',
    Name: 'Formula Field',
    Domain: 'TurbulenceField',
    Channel: 'velocity',
    Unit: 'm/s',
    Summary: 'A user-authored vector expression evaluated per sample, for generators the nineteen canonical laws do not cover.',
    Equation: 'v = S · [ f_x(x,y,z,t), f_y(x,y,z,t), f_z(x,y,z,t) ] · A(u)',
    Envelope: 'Box',
    Attenuation: 'SmoothStep',
    HalfExtents: [9, 9, 5],
    Glyph: VectorGlyph('<path d="M8.4 20.4c2.6 0 3.2-2 3.6-5l1.4-9.2c.4-2.6 1.6-3.6 3.6-3.6"/><path d="M6 10.4h8.4"/><path d="M14.6 14.6h6.8M16 12.4h3.6M16 16.8h3.6"/>'),
    Params: [
      Strength('m/s', 60, 6, 'Scale applied to the compiled expression.'),
      P('FormulaSpeed', 'Time scale', 0, 3, 0.01, 1, '[-]', 'Multiplier on t inside the expression.'),
    ],
    Formula: {
      Default: 'fx = sin(y * 0.9 + t * 0.6)\nfy = cos(x * 0.9 - t * 0.5)\nfz = 0.35 * sin((x + y) * 0.45 + t)',
      Channels: ['fx', 'fy', 'fz'],
    },
  },
]);

export const LawById = Object.fromEntries(FORCE_LAWS.map((law) => [law.Id, law]));

export const LawLabel = (id) => (LawById[id] ? LawById[id].Name : id);

export const DomainColor = (domainId) => (ForceDomains[domainId] ? ForceDomains[domainId].Color : '#9a9a9a');

export function ParamDefault(lawId, key) {
  const law = LawById[lawId];
  if (!law) return 0;
  const entry = law.Params.find((p) => p.Key === key);
  return entry ? entry.Default : 0;
}

export function ParamSchema(lawId) {
  const law = LawById[lawId];
  return law ? law.Params : [];
}

// Clamp and fill a source parameter block against the catalog, dropping unknown keys so imports stay safe.
export function SanitizeParams(lawId, values) {
  const out = {};
  for (const p of ParamSchema(lawId)) {
    const raw = values && Object.prototype.hasOwnProperty.call(values, p.Key) ? Number(values[p.Key]) : p.Default;
    out[p.Key] = Number.isFinite(raw) ? Math.min(p.Max, Math.max(p.Min, raw)) : p.Default;
  }
  return out;
}

export function SanitizeFormula(values) {
  const src = values || {};
  const code = typeof src.Code === 'string' ? src.Code.slice(0, 2000) : FORCE_LAWS[19].Formula.Default;
  return { Code: code };
}
