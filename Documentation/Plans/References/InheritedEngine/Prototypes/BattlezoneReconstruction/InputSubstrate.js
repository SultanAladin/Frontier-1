/*====================================================================================================================================
                                                      INPUTSUBSTRATE.JS
====================================================================================================================================*/
// 🧩 Keyboard and pointer-lock ingestion, decoded into a per-frame drive intent for the possessed chassis

import { ClampScalar } from "./LinearAlgebra.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const PointerYawSensitivity   = 0.0022;   // [rad/px] - Turret slew per horizontal pointer count
const PointerPitchSensitivity = 0.0016;   // [rad/px] - Barrel elevation per vertical pointer count

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT INGESTION
//------------------------------------------------------------------------------------------------------------------------

export class InputSubstrate
{
    constructor(TargetSurface)
    {
        this.TargetSurface      = TargetSurface;
        this.KeyAssignmentState = new Set();
        this.PointerSweepX      = 0.0;      // [px] - Accumulated horizontal pointer counts since the last decode
        this.PointerSweepY      = 0.0;      // [px] - Accumulated vertical pointer counts since the last decode
        this.DischargeHeld      = false;
        this.PointerLockState   = false;
        this.DiscreteRequests   = [];       // Edge-triggered key requests drained once per frame

        this.RegisterListeners();
    }

    RegisterListeners()
    {
        // 📝 Pointer lock is requested on click rather than at load — browsers reject an unsolicited lock request.
        this.TargetSurface.addEventListener("click", () =>
        {
            if (!this.PointerLockState) this.TargetSurface.requestPointerLock();
        });

        document.addEventListener("pointerlockchange", () =>
        {
            this.PointerLockState = document.pointerLockElement === this.TargetSurface;
        });

        document.addEventListener("mousemove", (PointerEvent) =>
        {
            if (!this.PointerLockState) return;
            this.PointerSweepX += PointerEvent.movementX;
            this.PointerSweepY += PointerEvent.movementY;
        });

        document.addEventListener("mousedown", (PointerEvent) =>
        {
            if (PointerEvent.button === 0) this.DischargeHeld = true;
        });

        document.addEventListener("mouseup", (PointerEvent) =>
        {
            if (PointerEvent.button === 0) this.DischargeHeld = false;
        });

        window.addEventListener("keydown", (KeyEvent) =>
        {
            const KeyIdentity = KeyEvent.code;
            if (!this.KeyAssignmentState.has(KeyIdentity)) this.DiscreteRequests.push(KeyIdentity);
            this.KeyAssignmentState.add(KeyIdentity);

            // Suppress page scroll on the driving and possession keys.
            if (["Space", "KeyQ", "KeyE", "KeyF", "Tab", "ArrowUp", "ArrowDown",
                 "ArrowLeft", "ArrowRight"].includes(KeyIdentity)) KeyEvent.preventDefault();
        });

        window.addEventListener("keyup", (KeyEvent) =>
        {
            this.KeyAssignmentState.delete(KeyEvent.code);
        });

        window.addEventListener("blur", () =>
        {
            this.KeyAssignmentState.clear();
            this.DischargeHeld = false;
        });
    }

    KeyEngaged(KeyIdentity)
    {
        return this.KeyAssignmentState.has(KeyIdentity);
    }

    // Drain the edge-triggered requests accumulated since the previous frame.
    ExtractDiscreteRequests()
    {
        const DrainedRequests = this.DiscreteRequests;
        this.DiscreteRequests = [];
        return DrainedRequests;
    }

    // 📝 Pointer counts are consumed here and zeroed, so a frame that renders late does not double-apply the sweep.
    DecodeDriveIntent()
    {
        let ThrottleAxis = 0.0;
        let StrafeIntent = 0.0;      // [-] - Screen sense: +1 means "sidestep toward screen-right"
        let SteerIntent  = 0.0;      // [-] - Screen sense: +1 means "turn the hull toward screen-right"
        let SlewIntent   = 0.0;      // [-] - Screen sense: +1 means "slew turret toward screen-right"
        let PitchIntent  = 0.0;      // [-] - Screen sense: +1 means "raise the barrel"

        // 📝 Modern first-person scheme: WASD drives and strafes relative to the SIGHT, mouse aims, and the arrow
        //    keys retain the original's explicit tank steering for anyone who wants to rotate the hull directly.
        if (this.KeyEngaged("KeyW") || this.KeyEngaged("ArrowUp"))    ThrottleAxis += 1.0;
        if (this.KeyEngaged("KeyS") || this.KeyEngaged("ArrowDown"))  ThrottleAxis -= 1.0;
        if (this.KeyEngaged("KeyD"))                                  StrafeIntent += 1.0;
        if (this.KeyEngaged("KeyA"))                                  StrafeIntent -= 1.0;
        if (this.KeyEngaged("ArrowRight"))                            SteerIntent  += 1.0;
        if (this.KeyEngaged("ArrowLeft"))                             SteerIntent  -= 1.0;

        if (this.KeyEngaged("KeyL")) SlewIntent  += 1.0;
        if (this.KeyEngaged("KeyJ")) SlewIntent  -= 1.0;
        if (this.KeyEngaged("KeyI")) PitchIntent += 1.0;
        if (this.KeyEngaged("KeyK")) PitchIntent -= 1.0;

        // 🔴 Screen sense is the INVERSE of yaw sense, so every YAW-carrying input is negated exactly once, here.
        //    Forward is (sinθ, cosθ); the camera's lateral (screen-right) axis is cross(Up, Backward) = (−cosθ, sinθ).
        //    d/dθ of forward is (cosθ, −sinθ), which dots to −1 against screen-right — a rising yaw sweeps LEFT.
        //
        // ⚠️ StrafeAxis is deliberately NOT negated: it is consumed as a camera-frame direction rather than an angle,
        //    and the simulation builds its strafe basis from that same (−cosθ, sinθ) lateral axis. Negating it here
        //    would invert sidestepping relative to the view it is supposed to track.
        const ScreenToYawSense = -1.0;

        const PointerYawSweep   = this.PointerSweepX * PointerYawSensitivity * ScreenToYawSense;
        const PointerPitchSweep = -this.PointerSweepY * PointerPitchSensitivity;

        this.PointerSweepX = 0.0;
        this.PointerSweepY = 0.0;

        return {
            ThrottleAxis:     ClampScalar(ThrottleAxis, -1.0, 1.0),
            StrafeAxis:       ClampScalar(StrafeIntent, -1.0, 1.0),
            SteerAxis:        ClampScalar(SteerIntent, -1.0, 1.0) * ScreenToYawSense,
            TurretSweep:      PointerYawSweep,
            TurretSlewAxis:   ClampScalar(SlewIntent, -1.0, 1.0) * ScreenToYawSense,
            PitchSweep:       PointerPitchSweep,
            PitchSlewAxis:    ClampScalar(PitchIntent, -1.0, 1.0),
            DischargeRequest: this.DischargeHeld || this.KeyEngaged("Space")
        };
    }
}
