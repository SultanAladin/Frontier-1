/*==========================================================================================================================================
                                                         CAMERANAVIGATION.JS
==========================================================================================================================================*/
// 🧩 Blender pointer gestures and time-based camera convergence shared by the HTML material viewports.

import { MathUtils, MOUSE, Spherical, Vector3 } from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";

export function EvaluateCameraLagWeight(Seconds, LagSeconds)
{
    if (!Number.isFinite(Seconds) || Seconds <= 0) return 0;
    if (!Number.isFinite(LagSeconds) || LagSeconds <= 0) return 1;
    return -Math.expm1(-Seconds / LagSeconds);
}

export class CameraNavigation
{
    constructor(ProjectionCamera, Canvas, LagSeconds = 0.12)
    {
        this.ProjectionCamera = ProjectionCamera;
        this.NavigationCamera = ProjectionCamera.clone();
        this.Controls = new OrbitControls(this.NavigationCamera, Canvas);
        this.Controls.enableDamping = false;
        this.Controls.enableZoom = false;
        this.Controls.mouseButtons = { LEFT: null, MIDDLE: MOUSE.ROTATE, RIGHT: null };
        this.LagSeconds = globalThis.matchMedia?.("(prefers-reduced-motion: reduce)").matches ? 0 : LagSeconds;
        this.VisibleTarget = this.Controls.target.clone();
        this.VisibleOrbit = new Spherical();
        this.TargetOrbit = new Spherical();
        this.Offset = new Vector3();
        this.Canvas = Canvas;
        this.DollyPointer = null;
        this.ConfigureZoomDelta = null;
        this.ConfigureFrame = null;
        Canvas.tabIndex = 0;
        Canvas.setAttribute("aria-label", "3D material viewport");
        Canvas.title = "MMB orbit · Shift+MMB pan · Ctrl+MMB zoom · Wheel zoom · Numpad . frame";
        Canvas.dataset.cameraLag = String(this.LagSeconds);
        this.BeginDolly = (Pointer) =>
        {
            if (!this.Controls.enabled || Pointer.button !== 1 || !Pointer.ctrlKey || this.DollyPointer) return;
            Pointer.preventDefault();
            Pointer.stopImmediatePropagation();
            this.DollyPointer = { Token: Pointer.pointerId, Y: Pointer.clientY };
            Canvas.setPointerCapture(Pointer.pointerId);
            Canvas.focus({ preventScroll: true });
        };
        this.MoveDolly = (Pointer) =>
        {
            if (this.DollyPointer?.Token !== Pointer.pointerId) return;
            Pointer.preventDefault();
            Pointer.stopImmediatePropagation();
            if (this.Controls.enabled) this.ConfigureZoomDelta?.(Pointer.clientY - this.DollyPointer.Y);
            this.DollyPointer.Y = Pointer.clientY;
        };
        this.FinalizeDolly = (Pointer) =>
        {
            if (this.DollyPointer?.Token !== Pointer.pointerId) return;
            this.DollyPointer = null;
            if (Canvas.hasPointerCapture(Pointer.pointerId)) Canvas.releasePointerCapture(Pointer.pointerId);
        };
        this.SuspendFocus = () =>
        {
            if (this.DollyPointer) this.FinalizeDolly({ pointerId: this.DollyPointer.Token });
        };
        this.FrameKey = (Keyboard) =>
        {
            if (Keyboard.code !== "NumpadDecimal" || !this.Controls.enabled) return;
            Keyboard.preventDefault();
            Keyboard.stopPropagation();
            this.ConfigureFrame?.();
        };
        Canvas.addEventListener("pointerdown", this.BeginDolly, true);
        Canvas.addEventListener("pointermove", this.MoveDolly, true);
        for (const EventName of ["pointerup", "pointercancel", "lostpointercapture"])
            Canvas.addEventListener(EventName, this.FinalizeDolly, true);
        Canvas.addEventListener("keydown", this.FrameKey);
        globalThis.addEventListener("blur", this.SuspendFocus);
    }

    QueryPending()
    {
        return this.ProjectionCamera.position.distanceToSquared(this.NavigationCamera.position) > 1e-12 ||
            this.VisibleTarget.distanceToSquared(this.Controls.target) > 1e-12 ||
            Math.abs(Math.log(this.ProjectionCamera.zoom / this.NavigationCamera.zoom)) > 1e-6;
    }

    Reset()
    {
        this.ProjectionCamera.position.copy(this.NavigationCamera.position);
        this.ProjectionCamera.quaternion.copy(this.NavigationCamera.quaternion);
        this.ProjectionCamera.zoom = this.NavigationCamera.zoom;
        this.VisibleTarget.copy(this.Controls.target);
        this.ProjectionCamera.updateProjectionMatrix();
        this.ProjectionCamera.updateMatrixWorld();
        this.Canvas.dataset.cameraPose = this.ProjectionCamera.position.toArray().map((Coordinate) => Coordinate.toFixed(6)).join(",");
        this.Canvas.dataset.cameraTarget = this.VisibleTarget.toArray().map((Coordinate) => Coordinate.toFixed(6)).join(",");
        this.Canvas.dataset.renderedZoom = String(this.ProjectionCamera.zoom);
    }

    Suspend()
    {
        // 📝 Discard unfinished navigation before painting; never move the camera under a stroke.
        this.NavigationCamera.position.copy(this.ProjectionCamera.position);
        this.NavigationCamera.zoom = this.ProjectionCamera.zoom;
        this.Controls.target.copy(this.VisibleTarget);
        this.Controls.update(0);
    }

    Evaluate(Seconds, AutomaticRotationEnabled = true)
    {
        const Δτ = Number.isFinite(Seconds) ? Math.max(0, Seconds) : 0;
        const AutomaticRotation = this.Controls.autoRotate;
        this.Controls.autoRotate = AutomaticRotation && AutomaticRotationEnabled;
        this.Controls.update(Math.min(Δτ, 0.1));
        this.Controls.autoRotate = AutomaticRotation;
        const Pending = this.QueryPending();
        if (!Pending) return false;
        const Weight = EvaluateCameraLagWeight(Δτ, this.LagSeconds);
        this.VisibleOrbit.setFromVector3(this.Offset.subVectors(this.ProjectionCamera.position, this.VisibleTarget));
        this.TargetOrbit.setFromVector3(this.Offset.subVectors(this.NavigationCamera.position, this.Controls.target));
        const θ = this.TargetOrbit.theta - this.VisibleOrbit.theta;
        this.VisibleOrbit.theta += Math.atan2(Math.sin(θ), Math.cos(θ)) * Weight;
        this.VisibleOrbit.phi = MathUtils.lerp(this.VisibleOrbit.phi, this.TargetOrbit.phi, Weight);
        this.VisibleOrbit.radius = MathUtils.lerp(this.VisibleOrbit.radius, this.TargetOrbit.radius, Weight);
        this.VisibleTarget.lerp(this.Controls.target, Weight);
        this.ProjectionCamera.position.copy(this.Offset.setFromSpherical(this.VisibleOrbit)).add(this.VisibleTarget);
        this.ProjectionCamera.lookAt(this.VisibleTarget);
        this.ProjectionCamera.zoom *= Math.exp(Math.log(this.NavigationCamera.zoom / this.ProjectionCamera.zoom) * Weight);
        this.ProjectionCamera.updateProjectionMatrix();
        this.ProjectionCamera.updateMatrixWorld();
        if (!this.QueryPending()) this.Reset();
        this.Canvas.dataset.cameraPose = this.ProjectionCamera.position.toArray().map((Coordinate) => Coordinate.toFixed(6)).join(",");
        this.Canvas.dataset.cameraTarget = this.VisibleTarget.toArray().map((Coordinate) => Coordinate.toFixed(6)).join(",");
        this.Canvas.dataset.renderedZoom = String(this.ProjectionCamera.zoom);
        return true;
    }

    Finalize()
    {
        this.SuspendFocus();
        this.Controls.dispose();
        this.Canvas.removeEventListener("pointerdown", this.BeginDolly, true);
        this.Canvas.removeEventListener("pointermove", this.MoveDolly, true);
        for (const EventName of ["pointerup", "pointercancel", "lostpointercapture"])
            this.Canvas.removeEventListener(EventName, this.FinalizeDolly, true);
        this.Canvas.removeEventListener("keydown", this.FrameKey);
        globalThis.removeEventListener("blur", this.SuspendFocus);
    }
}
