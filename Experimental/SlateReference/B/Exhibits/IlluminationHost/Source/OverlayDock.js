//============================================================================================================================================
//                                                              OVERLAYDOCK.JS
//============================================================================================================================================
// 🧩 The debug overlays — readout, distance-slice inset, control panel — closable, recallable from a dock, and remembered.

// 📝 Three overlays: the heads-up readout (top left of the viewport), the distance-slice inset the present pass draws
//    (top right, with its HTML legend and close button framed over it), and the editor's two columns. Each has
//    a close button and a key; whatever is closed reappears as a button in the dock at the bottom right. V hides all
//    of them (and the dock) for a clean view and brings back exactly what was open. Choices persist across reloads.

const StorageKey = "IlluminationExhibit.Overlays";

export const OverlayKeys = Object.freeze({ Hud: "KeyH", Inset: "KeyJ", Panel: "KeyP", Clean: "KeyV" });

const OverlayCaptions = Object.freeze({ Hud: "Readout (H)", Inset: "Distance slice (J)", Panel: "Editor (P)" });

export class OverlayDock
{
    // 📝 Settings carries HudVisible, InsetEnabled (read by the present pass) and PanelVisible; OnChange(Name) lets
    //    the host resynchronise the panel and rebuild screen targets when the panel's width enters or leaves the stage.
    constructor(Settings, OnChange)
    {
        this.Settings = Settings;
        this.OnChange = OnChange;
        this.Clean    = false;
        this.Retained = null;
        this.Elements = {
            Hud:    document.getElementById("hud"),
            Inset:  document.getElementById("inset-frame"),
            Dock:   document.getElementById("overlay-dock"),
            Notice: document.getElementById("overlay-notice")
        };
        this.RestoreChoices();
        this.ConstructDock();
        for (const Button of document.querySelectorAll("[data-overlay]"))
            Button.addEventListener("click", (Event) => { Event.stopPropagation(); this.Toggle(Button.dataset.overlay); });
        this.Apply();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   STATE
    //--------------------------------------------------------------------------------------------------------------------

    RestoreChoices()
    {
        try
        {
            const Stored = JSON.parse(localStorage.getItem(StorageKey) || "null");
            if (Stored && typeof Stored === "object")
            {
                for (const [Name, Key] of [["Hud", "HudVisible"], ["Inset", "InsetEnabled"], ["Panel", "PanelVisible"]])
                    if (typeof Stored[Name] === "boolean")
                        this.Settings[Key] = Stored[Name];
            }
        }
        catch (Failure)
        {
            // 📝 Storage may be unavailable (private mode, file://); the defaults simply stand.
        }
    }

    PersistChoices()
    {
        try
        {
            localStorage.setItem(StorageKey, JSON.stringify({ Hud: !!this.Settings.HudVisible, Inset: !!this.Settings.InsetEnabled,
                                                              Panel: !!this.Settings.PanelVisible }));
        }
        catch (Failure)
        {
        }
    }

    QueryVisible(Name)
    {
        if (this.Clean)
            return false;
        return !!this.Settings[{ Hud: "HudVisible", Inset: "InsetEnabled", Panel: "PanelVisible" }[Name]];
    }

    Toggle(Name)
    {
        if (Name === "Clean")
        {
            this.ToggleClean();
            return;
        }
        const Key = { Hud: "HudVisible", Inset: "InsetEnabled", Panel: "PanelVisible" }[Name];
        if (!Key)
            return;
        if (this.Clean)
        {
            // 📝 Asking for one overlay out of the clean view brings the view back first, then shows that overlay.
            this.Clean = false;
            this.Settings[Key] = true;
        }
        else
        {
            this.Settings[Key] = !this.Settings[Key];
        }
        this.Apply();
        this.OnChange(Name);
    }

    ToggleClean()
    {
        this.Clean = !this.Clean;
        this.Apply();
        this.OnChange("Clean");
        if (this.Clean)
            this.Announce("Clean view — press V to bring the overlays back");
    }

    // 📝 Called when the panel's own toggles change a visibility key, so the dock and frames follow.
    Synchronise()
    {
        this.Apply();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                PRESENTATION
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 The editor's viewport bar carries the readout and inset toggles; the dock only has to bring the editor back.
    ConstructDock()
    {
        this.DockButtons = {};
        for (const Name of ["Panel"])
        {
            const Button = document.createElement("button");
            Button.textContent = `⊞ ${OverlayCaptions[Name]}`;
            Button.title = `Show the ${OverlayCaptions[Name].toLowerCase()}`;
            Button.addEventListener("click", () => this.Toggle(Name));
            this.Elements.Dock.appendChild(Button);
            this.DockButtons[Name] = Button;
        }
    }

    Apply()
    {
        const Hud = this.QueryVisible("Hud"), Inset = this.QueryVisible("Inset"), Panel = this.QueryVisible("Panel");
        this.Elements.Hud.classList.toggle("hidden", !Hud);
        this.Elements.Inset.classList.toggle("hidden", !Inset);
        // 📝 The editor's outliner and inspector columns fold away together; the viewport takes their room.
        document.body.classList.toggle("panels-hidden", !Panel);
        // 📝 The present pass draws the inset; in the clean view it must stop drawing it without forgetting the choice.
        this.InsetDrawn = Inset;
        let Closed = 0;
        for (const [Name, Visible] of [["Panel", Panel]])
        {
            this.DockButtons[Name].classList.toggle("hidden", Visible);
            Closed += Visible ? 0 : 1;
        }
        this.Elements.Dock.classList.toggle("hidden", this.Clean || Closed === 0);
        this.PersistChoices();
    }

    Announce(Text)
    {
        const Notice = this.Elements.Notice;
        if (!Notice)
            return;
        Notice.textContent = Text;
        Notice.classList.remove("hidden");
        clearTimeout(this.NoticeTimer);
        this.NoticeTimer = setTimeout(() => Notice.classList.add("hidden"), 2600);
    }

    // 📝 Frames the HTML close button and legend over the inset DisplayPresent.wgsl draws: a square of ⌊0.34·H⌋ device
    //    pixels, 16 device pixels from the top-right corner of the render target, converted here to CSS pixels.
    PlaceInset(Canvas, ScreenExtent)
    {
        if (!ScreenExtent || !Canvas.clientWidth)
            return;
        const Ratio = ScreenExtent[0] / Canvas.clientWidth;
        const Size = Math.floor(ScreenExtent[1] * 0.34);
        const Style = this.Elements.Inset.style;
        Style.left   = `${(ScreenExtent[0] - Size - 16) / Ratio}px`;
        Style.top    = `${16 / Ratio}px`;
        Style.width  = `${Size / Ratio}px`;
        Style.height = `${Size / Ratio}px`;
    }
}
