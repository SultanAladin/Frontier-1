// DISPLAY KIT
// 🧩 Small pieces every cabin HMI prototype shares: fixed-size display scaling, an eased camera orbit, anchor
//    projection for 3D callouts, number rolling and press ripples.

import { THREE } from './VehicleAssembly.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    DISPLAY SCALING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each HMI is authored at one fixed pixel size (a real cabin panel) and uniformly scaled to fit the window.
export function FitDisplay(Element, Width, Height, Margin = 32)
{
    const Apply = () =>
    {
        const Scale = Math.min((window.innerWidth - Margin * 2) / Width, (window.innerHeight - Margin * 2) / Height);
        Element.style.width     = `${Width}px`;
        Element.style.height    = `${Height}px`;
        Element.style.transform = `translate(-50%, -50%) scale(${Math.max(0.2, Scale)})`;
    };
    window.addEventListener('resize', Apply);
    Apply();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CAMERA ORBIT
//------------------------------------------------------------------------------------------------------------------------

const Eased = (Ratio) => (Ratio < 0.5 ? 4 * Ratio * Ratio * Ratio : 1 - Math.pow(-2 * Ratio + 2, 3) / 2);

export class CameraOrbit
{
    constructor(Camera, Surface, { Target = [0, 0.7, 0], Radius = 9, Yaw = 0.8, Pitch = 0.22, RadiusLimits = [5, 16], PitchLimits = [-0.02, 1.45] } = {})
    {
        this.Camera       = Camera;
        this.Target       = new THREE.Vector3(...Target);
        this.Radius       = Radius;
        this.Yaw          = Yaw;
        this.Pitch        = Pitch;
        this.RadiusLimits = RadiusLimits;
        this.PitchLimits  = PitchLimits;
        this.Transit      = null;                                 // active eased move between presets
        this.Drift        = 0;                                    // [rad/s] - idle turntable speed
        this.Parallax     = [0, 0];                               // pointer-follow offset (micro motion)
        this.ParallaxGoal = [0, 0];
        this.BindPointer(Surface);
        this.Place();
    }

    BindPointer(Surface)
    {
        let Origin = null;
        Surface.addEventListener('pointerdown', (Press) => { Origin = [Press.clientX, Press.clientY, this.Yaw, this.Pitch]; this.Transit = null; Surface.setPointerCapture(Press.pointerId); });
        Surface.addEventListener('pointerup', () => { Origin = null; });
        Surface.addEventListener('pointermove', (Motion) =>
        {
            const Box = Surface.getBoundingClientRect();
            this.ParallaxGoal = [(Motion.clientX - Box.left) / Box.width - 0.5, (Motion.clientY - Box.top) / Box.height - 0.5];
            if (!Origin) return;
            this.Yaw   = Origin[2] - (Motion.clientX - Origin[0]) * 0.006;
            this.Pitch = THREE.MathUtils.clamp(Origin[3] + (Motion.clientY - Origin[1]) * 0.004, ...this.PitchLimits);
        });
        Surface.addEventListener('wheel', (Turn) => { Turn.preventDefault(); this.Radius = THREE.MathUtils.clamp(this.Radius * (1 + Turn.deltaY * 0.001), ...this.RadiusLimits); }, { passive: false });
    }

    // Ease to a preset { Target, Radius, Yaw, Pitch } over Duration ms; the shortest yaw arc is taken.
    Aim(Preset, Duration = 1100)
    {
        const From = { Target: this.Target.clone(), Radius: this.Radius, Yaw: this.Yaw, Pitch: this.Pitch };
        let Yaw    = Preset.Yaw ?? this.Yaw;
        while (Yaw - From.Yaw > Math.PI) Yaw -= 2 * Math.PI;
        while (Yaw - From.Yaw < -Math.PI) Yaw += 2 * Math.PI;
        this.Transit = { From, To: { Target: new THREE.Vector3(...(Preset.Target || this.Target.toArray())), Radius: Preset.Radius ?? this.Radius, Yaw, Pitch: Preset.Pitch ?? this.Pitch }, Begin: performance.now(), Duration };
    }

    Advance(Delta)
    {
        if (this.Transit)
        {
            const { From, To, Begin, Duration } = this.Transit;
            const Ratio = Math.min(1, (performance.now() - Begin) / Duration);
            const Mix   = Eased(Ratio);
            this.Target.lerpVectors(From.Target, To.Target, Mix);
            this.Radius = From.Radius + (To.Radius - From.Radius) * Mix;
            this.Yaw    = From.Yaw + (To.Yaw - From.Yaw) * Mix;
            this.Pitch  = From.Pitch + (To.Pitch - From.Pitch) * Mix;
            if (Ratio >= 1) this.Transit = null;
        }
        else
        {
            this.Yaw += this.Drift * Delta;
        }
        const Follow = 1 - Math.exp(-Delta * 3);
        this.Parallax[0] += (this.ParallaxGoal[0] - this.Parallax[0]) * Follow;
        this.Parallax[1] += (this.ParallaxGoal[1] - this.Parallax[1]) * Follow;
        this.Place();
    }

    Place()
    {
        const Yaw   = this.Yaw + this.Parallax[0] * 0.06;
        const Pitch = this.Pitch + this.Parallax[1] * 0.03;
        const Cos   = Math.cos(Pitch);
        this.Camera.position.set(this.Target.x + this.Radius * Cos * Math.sin(Yaw), this.Target.y + this.Radius * Math.sin(Pitch), this.Target.z + this.Radius * Cos * Math.cos(Yaw));
        this.Camera.lookAt(this.Target);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ANCHOR PROJECTION
//------------------------------------------------------------------------------------------------------------------------

const Scratch = new THREE.Vector3();

// World point → display pixels (design space of the fixed-size display). Visible is false behind the camera.
export function ProjectAnchor(Camera, Point, Width, Height)
{
    Scratch.copy(Point).project(Camera);
    return { X: (Scratch.x * 0.5 + 0.5) * Width, Y: (-Scratch.y * 0.5 + 0.5) * Height, Visible: Scratch.z < 1 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MICRO ANIMATIONS
//------------------------------------------------------------------------------------------------------------------------

const RollTargets = new WeakMap();

// 📝 Rolls a number from its last shown figure to Target; Format receives the eased figure.
export function RollNumber(Element, Target, { Duration = 520, Format = (Figure) => Figure.toFixed(0) } = {})
{
    const Start = RollTargets.get(Element) ?? Target;
    const Begin = performance.now();
    RollTargets.set(Element, Target);
    const Advance = (Now) =>
    {
        const Ratio = Math.min(1, (Now - Begin) / Duration);
        Element.textContent = Format(Start + (Target - Start) * (1 - Math.pow(1 - Ratio, 3)));
        if (Ratio < 1) requestAnimationFrame(Advance);
    };
    requestAnimationFrame(Advance);
}

// Press ripple on every element matching Selector inside Scope (delegated, so late-built buttons ripple too).
export function BindRipples(Scope, Selector = '[data-ripple]')
{
    Scope.addEventListener('pointerdown', (Press) =>
    {
        const Host = Press.target.closest(Selector);
        if (!Host) return;
        const Box    = Host.getBoundingClientRect();
        const Scale  = Box.width / Host.offsetWidth || 1;
        const Wave   = document.createElement('span');
        const Size   = Math.max(Host.offsetWidth, Host.offsetHeight) * 2;
        Wave.className   = 'RippleWave';
        Wave.style.width = Wave.style.height = `${Size}px`;
        Wave.style.left  = `${(Press.clientX - Box.left) / Scale - Size / 2}px`;
        Wave.style.top   = `${(Press.clientY - Box.top) / Scale - Size / 2}px`;
        Host.append(Wave);
        Wave.addEventListener('animationend', () => Wave.remove());
    });
}

// Clock text for status bars.
export function ClockText(Date24 = new Date())
{
    return `${String(Date24.getHours()).padStart(2, '0')}:${String(Date24.getMinutes()).padStart(2, '0')}`;
}

// Render loop with a clamped delta; Step receives (Delta [s], Elapsed [s]).
export function RunLoop(Step)
{
    let Last = performance.now();
    const Begin = Last;
    const Tick = (Now) =>
    {
        const Delta = Math.min(0.05, (Now - Last) / 1000);
        Last = Now;
        Step(Delta, (Now - Begin) / 1000);
        requestAnimationFrame(Tick);
    };
    requestAnimationFrame(Tick);
}
