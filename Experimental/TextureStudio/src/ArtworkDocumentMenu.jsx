/*============================================================================================================================================
                                                        ARTWORKDOCUMENTMENU.JSX
============================================================================================================================================*/
// 🧩 Keep less-frequent document actions discoverable without another permanent row of header buttons.

import React from "react";
import { MoreHorizontal } from "lucide-react";
export default function ArtworkDocumentMenu({ children })
{
    return <details className="se-tool-menu artwork-document-menu" name="vector-toolbar-menus" onClick={Event => { if (Event.target.closest("button")) Event.currentTarget.removeAttribute("open"); }}>
        <summary aria-label="Document actions" title="Document actions"><MoreHorizontal size={16}/></summary>
        <div className="se-menu-content">{children}</div>
    </details>;
}
