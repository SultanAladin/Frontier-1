/*====================================================================================================================================
                                                       COMBATOVERLAY.JS
====================================================================================================================================*/
// 🧩 Canvas-2D gunsight reticle, radar sweep, armour readout and command notices drawn over the rasterized field

import { FieldSpecification, ArmamentRegistry } from "./CombatSpecification.js";
import { Vec3GroundDistance, ShortestAngularSweep, ClampScalar } from "./LinearAlgebra.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const OverlayTone        = "#7cf6b0";     // Primary vector-display tone
const OverlayWarningTone = "#ff7b46";     // Attrition and depletion tone
const RadarRadiusPixels  = 92.0;          // [px] - Radar disc radius
const RadarFieldRange    = 380.0;         // [m]  - Ground distance mapped to the radar edge

//------------------------------------------------------------------------------------------------------------------------
//                                                    OVERLAY DRAWING
//------------------------------------------------------------------------------------------------------------------------

export class CombatOverlay
{
    constructor(OverlaySurface)
    {
        this.OverlaySurface = OverlaySurface;
        this.DrawContext    = OverlaySurface.getContext("2d");
    }

    // 📝 Every sub-panel is drawn from scratch each frame; at this element count a full repaint is cheaper than
    //    tracking dirty regions, and it keeps the radar sweep free of ghosting.
    RenderOverlay(CombatFieldInstance, FrameRate)
    {
        const DrawContext  = this.DrawContext;
        const SurfaceWidth  = this.OverlaySurface.width;
        const SurfaceHeight = this.OverlaySurface.height;
        DrawContext.clearRect(0, 0, SurfaceWidth, SurfaceHeight);

        const PossessedChassis = CombatFieldInstance.PossessedChassis();
        if (!PossessedChassis) return;

        this.RenderReticle(SurfaceWidth, SurfaceHeight, PossessedChassis);
        this.RenderRadarDisc(SurfaceWidth, SurfaceHeight, CombatFieldInstance, PossessedChassis);
        this.RenderArmourReadout(SurfaceWidth, SurfaceHeight, PossessedChassis);
        this.RenderArmamentReadout(SurfaceWidth, SurfaceHeight, PossessedChassis);
        this.RenderCommandNotices(SurfaceWidth, SurfaceHeight, CombatFieldInstance);
        this.RenderFieldTally(SurfaceWidth, SurfaceHeight, CombatFieldInstance, FrameRate);
    }

    RenderReticle(SurfaceWidth, SurfaceHeight, PossessedChassis)
    {
        const DrawContext = this.DrawContext;
        const CentreX = SurfaceWidth * 0.5;
        const CentreY = SurfaceHeight * 0.5;

        // Barrel elevation biases the reticle vertically so the sight reflects where the shell will actually go.
        const ElevationOffset = -PossessedChassis.BarrelPitch * SurfaceHeight * 0.55;
        const ReloadFraction  = ClampScalar(PossessedChassis.ReloadRemaining /
            ArmamentRegistry[PossessedChassis.ArmamentLabel].ReloadInterval, 0.0, 1.0);

        DrawContext.strokeStyle = ReloadFraction > 0.0 ? OverlayWarningTone : OverlayTone;
        DrawContext.lineWidth   = 1.6;
        DrawContext.globalAlpha = 0.92;

        const ReticleGap  = 12.0;
        const ReticleSpan = 30.0;
        DrawContext.beginPath();
        DrawContext.moveTo(CentreX - ReticleSpan, CentreY + ElevationOffset);
        DrawContext.lineTo(CentreX - ReticleGap,  CentreY + ElevationOffset);
        DrawContext.moveTo(CentreX + ReticleGap,  CentreY + ElevationOffset);
        DrawContext.lineTo(CentreX + ReticleSpan, CentreY + ElevationOffset);
        DrawContext.moveTo(CentreX, CentreY + ElevationOffset - ReticleSpan);
        DrawContext.lineTo(CentreX, CentreY + ElevationOffset - ReticleGap);
        DrawContext.moveTo(CentreX, CentreY + ElevationOffset + ReticleGap);
        DrawContext.lineTo(CentreX, CentreY + ElevationOffset + ReticleSpan);
        DrawContext.stroke();

        // Reload arc closing clockwise around the reticle.
        if (ReloadFraction > 0.0)
        {
            DrawContext.beginPath();
            DrawContext.arc(CentreX, CentreY + ElevationOffset, 42.0, -Math.PI * 0.5,
                            -Math.PI * 0.5 + Math.PI * 2.0 * (1.0 - ReloadFraction));
            DrawContext.stroke();
        }

        // Horizon ladder — a faint level reference the original's wireframe horizon provided for free.
        DrawContext.globalAlpha = 0.28;
        DrawContext.beginPath();
        DrawContext.moveTo(CentreX - 190, CentreY);
        DrawContext.lineTo(CentreX - 70,  CentreY);
        DrawContext.moveTo(CentreX + 70,  CentreY);
        DrawContext.lineTo(CentreX + 190, CentreY);
        DrawContext.stroke();
        DrawContext.globalAlpha = 1.0;
    }

    RenderRadarDisc(SurfaceWidth, SurfaceHeight, CombatFieldInstance, PossessedChassis)
    {
        const DrawContext = this.DrawContext;
        const DiscCentreX = SurfaceWidth - RadarRadiusPixels - 34.0;
        const DiscCentreY = RadarRadiusPixels + 34.0;

        DrawContext.fillStyle = "rgba(4, 16, 12, 0.62)";
        DrawContext.beginPath();
        DrawContext.arc(DiscCentreX, DiscCentreY, RadarRadiusPixels, 0, Math.PI * 2.0);
        DrawContext.fill();

        DrawContext.strokeStyle = OverlayTone;
        DrawContext.globalAlpha = 0.55;
        DrawContext.lineWidth   = 1.3;
        DrawContext.stroke();

        DrawContext.beginPath();
        DrawContext.arc(DiscCentreX, DiscCentreY, RadarRadiusPixels * 0.5, 0, Math.PI * 2.0);
        DrawContext.stroke();

        // Forward-facing sight wedge, drawn in the radar frame where "up" is the turret axis.
        const WedgeHalfAngle = 0.34;
        DrawContext.globalAlpha = 0.18;
        DrawContext.fillStyle   = OverlayTone;
        DrawContext.beginPath();
        DrawContext.moveTo(DiscCentreX, DiscCentreY);
        DrawContext.arc(DiscCentreX, DiscCentreY, RadarRadiusPixels,
                        -Math.PI * 0.5 - WedgeHalfAngle, -Math.PI * 0.5 + WedgeHalfAngle);
        DrawContext.closePath();
        DrawContext.fill();
        DrawContext.globalAlpha = 1.0;

        // 🔴 Contacts are plotted in the TURRET frame, not world north — the sweep must agree with the reticle or
        //    the radar actively misleads while the turret is off-axis.
        for (const ContactChassis of CombatFieldInstance.ChassisRoster)
        {
            if (ContactChassis === PossessedChassis) continue;

            const GroundSeparation = Vec3GroundDistance(PossessedChassis.GroundPosition, ContactChassis.GroundPosition);
            if (GroundSeparation > RadarFieldRange) continue;

            const WorldBearing = Math.atan2(
                ContactChassis.GroundPosition[0] - PossessedChassis.GroundPosition[0],
                ContactChassis.GroundPosition[2] - PossessedChassis.GroundPosition[2]);
            const RelativeBearing = ShortestAngularSweep(PossessedChassis.TurretAngle, WorldBearing);
            const PlotRadius      = (GroundSeparation / RadarFieldRange) * RadarRadiusPixels;

            // 🔴 Negated for the same reason the yaw inputs are: screen-right is world −X, so a positive relative
            //    bearing lies to screen-LEFT. Without this the radar mirrors the view and actively misleads.
            const PlotX = DiscCentreX - Math.sin(RelativeBearing) * PlotRadius;
            const PlotY = DiscCentreY - Math.cos(RelativeBearing) * PlotRadius;

            const ContactExtent = ContactChassis.ClassLabel === "Heavy" ? 4.6 :
                                 (ContactChassis.ClassLabel === "Medium" ? 3.6 : 2.8);
            DrawContext.fillStyle = ContactChassis.DestroyedState ? "#4b5b55"
                                  : `rgb(${ContactChassis.ClassProfile.ChassisTone.map(
                                        ToneScalar => Math.round(ToneScalar * 255)).join(",")})`;
            DrawContext.beginPath();
            DrawContext.arc(PlotX, PlotY, ContactExtent, 0, Math.PI * 2.0);
            DrawContext.fill();
        }

        DrawContext.fillStyle = "#ffffff";
        DrawContext.beginPath();
        DrawContext.arc(DiscCentreX, DiscCentreY, 2.6, 0, Math.PI * 2.0);
        DrawContext.fill();
    }

    RenderArmourReadout(SurfaceWidth, SurfaceHeight, PossessedChassis)
    {
        const DrawContext   = this.DrawContext;
        const PanelOriginX  = 34.0;
        const PanelOriginY  = SurfaceHeight - 92.0;
        const ArmourBarSpan = 244.0;
        const ArmourFraction = ClampScalar(
            PossessedChassis.ArmourRemaining / PossessedChassis.ClassProfile.ArmourCapacity, 0.0, 1.0);

        DrawContext.font      = "600 13px 'Consolas', 'Courier New', monospace";
        DrawContext.fillStyle = OverlayTone;
        DrawContext.fillText(`${PossessedChassis.ClassProfile.ClassLabel}`, PanelOriginX, PanelOriginY - 10);

        DrawContext.strokeStyle = OverlayTone;
        DrawContext.lineWidth   = 1.3;
        DrawContext.globalAlpha = 0.75;
        DrawContext.strokeRect(PanelOriginX, PanelOriginY, ArmourBarSpan, 15);
        DrawContext.globalAlpha = 1.0;

        DrawContext.fillStyle = ArmourFraction < 0.3 ? OverlayWarningTone : OverlayTone;
        DrawContext.fillRect(PanelOriginX + 2, PanelOriginY + 2, (ArmourBarSpan - 4) * ArmourFraction, 11);

        DrawContext.fillStyle = "#d8fff0";
        DrawContext.font      = "500 11px 'Consolas', 'Courier New', monospace";
        DrawContext.fillText(`ARMOUR ${Math.round(PossessedChassis.ArmourRemaining)} / ` +
                             `${Math.round(PossessedChassis.ClassProfile.ArmourCapacity)}`,
                             PanelOriginX, PanelOriginY + 32);
        DrawContext.fillText(`SPEED ${Math.abs(PossessedChassis.ForwardVelocity).toFixed(1)} m/s`,
                             PanelOriginX + 140, PanelOriginY + 32);
    }

    RenderArmamentReadout(SurfaceWidth, SurfaceHeight, PossessedChassis)
    {
        const DrawContext  = this.DrawContext;
        const PanelOriginX = SurfaceWidth - 250.0;
        let   PanelOriginY = SurfaceHeight - 96.0;

        DrawContext.font = "600 12px 'Consolas', 'Courier New', monospace";
        for (const ArmamentLabel of ["Cannon", "Rocket", "ArcMortar"])
        {
            const ArmamentProfile = ArmamentRegistry[ArmamentLabel];
            const ReserveTally    = PossessedChassis.ArmamentReserves[ArmamentLabel];
            const SelectedState   = PossessedChassis.ArmamentLabel === ArmamentLabel;

            DrawContext.globalAlpha = ReserveTally > 0 ? 1.0 : 0.32;
            DrawContext.fillStyle   = SelectedState ? OverlayTone : "#8ba79c";
            const ReserveText = ReserveTally === Infinity ? "∞" : `${ReserveTally}`;
            DrawContext.fillText(`${SelectedState ? "▶" : " "} ${ArmamentProfile.ArmamentLabel}`,
                                 PanelOriginX, PanelOriginY);
            DrawContext.fillText(ReserveText, PanelOriginX + 196, PanelOriginY);
            PanelOriginY += 20;
        }
        DrawContext.globalAlpha = 1.0;
    }

    RenderCommandNotices(SurfaceWidth, SurfaceHeight, CombatFieldInstance)
    {
        const DrawContext = this.DrawContext;
        DrawContext.font      = "600 14px 'Consolas', 'Courier New', monospace";
        DrawContext.textAlign = "center";

        let NoticeOriginY = SurfaceHeight * 0.24;
        for (const NoticeEntry of CombatFieldInstance.CommandNotices)
        {
            DrawContext.globalAlpha = ClampScalar(NoticeEntry.RemainingSeconds / 1.2, 0.0, 1.0);
            DrawContext.fillStyle   = OverlayTone;
            DrawContext.fillText(NoticeEntry.NoticeText, SurfaceWidth * 0.5, NoticeOriginY);
            NoticeOriginY += 22;
        }

        DrawContext.globalAlpha = 1.0;
        DrawContext.textAlign   = "left";
    }

    RenderFieldTally(SurfaceWidth, SurfaceHeight, CombatFieldInstance, FrameRate)
    {
        const DrawContext = this.DrawContext;
        const SurvivingTally = CombatFieldInstance.ChassisRoster.filter(
            ChassisEntry => !ChassisEntry.DestroyedState).length;

        DrawContext.font      = "500 11px 'Consolas', 'Courier New', monospace";
        DrawContext.fillStyle = "#7f9c91";
        DrawContext.fillText(`ELIMINATED ${CombatFieldInstance.EliminationTally}`, 34, 30);
        DrawContext.fillText(`CHASSIS ON FIELD ${SurvivingTally}`, 34, 46);
        DrawContext.fillText(`${FrameRate.toFixed(0)} fps`, 34, 62);
    }
}
