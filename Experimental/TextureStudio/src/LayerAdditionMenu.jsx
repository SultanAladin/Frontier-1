/*============================================================================================================================================
                                                          LAYERADDITIONMENU.JSX
============================================================================================================================================*/
// 🧩 Keyboard-accessible layer insertion choices anchored to a shared stack heading.

import React, { useEffect, useLayoutEffect, useRef, useState } from "react";
import { createPortal } from "react-dom";
import { Plus } from "lucide-react";
import "./layerAdditionMenu.css";

export default function LayerAdditionMenu({ Caption, Choices, Disabled, OnChoose })
{
    const [Expanded, ConfigureExpanded] = useState(false);
    const [Position, ConfigurePosition] = useState(null);
    const Anchor = useRef(null), Menu = useRef(null);
    useLayoutEffect(() =>
    {
        if (!Expanded) return;
        const Bounds = Anchor.current.getBoundingClientRect();
        const Height = Menu.current.getBoundingClientRect().height;
        ConfigurePosition({ left: Math.max(12, Math.min(Bounds.right - 240, innerWidth - 252)), top: Math.max(12, Math.min(Bounds.bottom + 8, innerHeight - Height - 12)) });
    }, [Expanded]);
    useLayoutEffect(() =>
    {
        if (Expanded && Position) Menu.current?.querySelector("button")?.focus();
    }, [Expanded, Position]);
    useEffect(() =>
    {
        if (!Expanded) return;
        function Dismiss(Event)
        {
            if (Event.type === "pointerdown" && (Anchor.current?.contains(Event.target) || Menu.current?.contains(Event.target))) return;
            if (Event.type === "keydown" && Event.key !== "Escape") return;
            if (Event.type === "keydown") { Event.preventDefault(); Event.stopPropagation(); Anchor.current?.focus(); }
            ConfigureExpanded(false);
        }
        document.addEventListener("pointerdown", Dismiss, true);
        document.addEventListener("keydown", Dismiss, true);
        window.addEventListener("resize", Dismiss);
        return () =>
        {
            document.removeEventListener("pointerdown", Dismiss, true);
            document.removeEventListener("keydown", Dismiss, true);
            window.removeEventListener("resize", Dismiss);
        };
    }, [Expanded]);
    function Navigate(Event)
    {
        if (Event.key === "Tab") { ConfigureExpanded(false); Anchor.current?.focus(); return; }
        if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(Event.key)) return;
        Event.preventDefault();
        const Buttons = [...Menu.current.querySelectorAll("button")];
        const Index = Buttons.indexOf(document.activeElement);
        Buttons[Event.key === "Home" ? 0 : Event.key === "End" ? Buttons.length - 1 : (Index + (Event.key === "ArrowUp" ? -1 : 1) + Buttons.length) % Buttons.length]?.focus();
    }
    return <>
        <button ref={Anchor} className="tp-round-icon" aria-label={Caption} aria-haspopup="menu" aria-expanded={Expanded} disabled={Disabled} title={Caption} onClick={() => { ConfigurePosition(null); ConfigureExpanded(!Expanded); }}><Plus size={18}/></button>
        {Expanded && createPortal(<div ref={Menu} className="layer-addition-menu studio-themed" role="menu" aria-label={Caption} style={{ ...Position, visibility: Position ? "visible" : "hidden" }} onKeyDown={Navigate}>
            {Choices.map(({ Caption: Label, Icon, Category }) => <button role="menuitem" key={Category} onClick={() => { if (OnChoose(Category, Label) === false) return; ConfigureExpanded(false); Anchor.current?.focus(); }}>{Icon && <Icon size={15}/>}<span>{Label}</span></button>)}
        </div>, document.body)}
    </>;
}
