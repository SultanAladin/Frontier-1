/*==========================================================================================================================================
                                                               BROWSER.JS
==========================================================================================================================================*/
// 🧩 Reference-rendered previews, selectable sample revisions and local file access; native hand-off remains explicit.
'use strict';

//------------------------------------------------------------------------------------------------------------------------
//                                                     FILE CATALOGUE AND PREFERENCES
//------------------------------------------------------------------------------------------------------------------------

const Query = Selector => document.querySelector(Selector);
const Escape = Text => String(Text).replace(/[&<>"']/g, Character => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[Character]));
const Icon = Name => `<svg class="icon" aria-hidden="true"><use href="#${Name}"/></svg>`;
function RetrievePreference(Key, Fallback)
{
    try { return JSON.parse(localStorage.getItem(Key)) ?? Fallback; } catch { return Fallback; }
}
function PersistPreference(Key, Content)
{
    try { localStorage.setItem(Key, JSON.stringify(Content)); } catch { Notify('Browser storage is unavailable. Changes last for this session only.'); }
}
// 🧩 Default Project only (Project Zero specification §14, decision 3 Option A). The six sample files (Actuator studio, Precision mount,
// Actuator housing, Studio exploration, Actuator sketches, Housing study) were removed. "+ New Project" adds a blank scene that opens in
// Frontier only. 📝 The Default Project preview still uses the reference studio renderer, not the project's real contents (native decoding
// is not connected).
const DefaultProjectToken = 'default';
const Catalogue = [
    {Token:DefaultProjectToken,Title:'Default Project',Extension:'.projectspace',Category:'project',Description:'The project Frontier opens with. Reference studio preview until native project decoding is connected.',Art:'Valley',Context:'1 scene · default',Opened:'Always available',Path:'Projects/Default Project/Default.projectspace',Views:['Default scene'],Notes:[],Revisions:['Current']}
];
let NewProjectSequence = 0;
function CreateProject()
{
    NewProjectSequence += 1;
    const Title = NewProjectSequence === 1 ? 'New Project' : `New Project ${NewProjectSequence}`;
    const Entry = {Token:`new-${Date.now()}-${NewProjectSequence}`,Title,Extension:'.projectspace',Category:'project',Blank:true,Imported:true,Description:'A blank scene. It opens in Frontier only; no other workspace is launched.',Art:'Valley',Context:'Blank scene · this session',Opened:'Just now',Path:`Projects/${Title}/${Title.replace(/ /g,'')}.projectspace`,Views:['Blank scene'],Notes:[],Revisions:[]};
    Catalogue.push(Entry);
    ActiveFilter = 'all'; Query('#Search').value = '';
    document.querySelectorAll('[data-filter]').forEach(Button => Button.classList.toggle('active',Button.dataset.filter === 'all'));
    ConstructCatalogue();
    Notify(`${Title} created as a blank scene for this session. Saving .projectspace files needs the native editor.`);
    return Entry;
}
// 🧩 Two native editors can open a file: Frontier (scenes, levels, play) and SolidArc (CAD modelling). One choice per format.
const Applications = {
    frontier:{Title:'Frontier',Letter:'A',Mark:'frontier-mark',Caption:'Game editor',Opens:'Scenes, levels, lighting and play',Workspace:'Project workspace'},
    solidarc:{Title:'SolidArc',Letter:'B',Mark:'solidarc-mark',Caption:'CAD modelling',Opens:'Parts, sketches and assemblies',Workspace:'Modelling workspace'}
};
const FormatTitles = {project:'Projects',cad:'CAD',geometry:'Geometry',document:'Documents'};
const OpenWith = {project:'frontier',cad:'solidarc',geometry:'solidarc',document:'frontier',...RetrievePreference('frontier.browser.openWith',{})};
for (const Format of Object.keys(OpenWith)) if (!Applications[OpenWith[Format]]) delete OpenWith[Format];
function QueryApplication(Entry){return OpenWith[Entry.Category] || 'frontier';}
function ConfigureApplication(Category, Application)
{
    if (!Applications[Application]) return;
    OpenWith[Category] = Application;
    PersistPreference('frontier.browser.openWith', OpenWith);
    ConstructCatalogue();
    ConstructLibraryOpenWith();
    if (Selected) { ConstructOpenControls(); ConstructInspector(); }
}
function ConstructApplicationChoices(Category, Current, Compact)
{
    return `<div class="ow-choices${Compact ? ' compact' : ''}" role="radiogroup" aria-label="Open ${Escape(FormatTitles[Category] || 'files')} with">${Object.entries(Applications).map(([Token,Application]) =>
        `<button type="button" role="radio" class="${Token===Current ? 'on' : ''}" aria-checked="${Token===Current}" data-category="${Category}" data-application="${Token}"><b>${Application.Letter}</b>${Icon(Application.Mark)}<span>${Application.Title}</span>${Compact ? '' : `<small>${Application.Caption}</small>`}</button>`).join('')}</div>`;
}
function ConstructOpenWithCard(Entry)
{
    const Token = QueryApplication(Entry), Application = Applications[Token];
    return `<section class="pz-card open-with-card" data-card="Open with"><h3>${Icon('arrow')}Open with<span class="pz-card-note">${Escape(FormatTitles[Entry.Category] || 'Files')}</span></h3>
        <div class="ow-hero" data-application-shown="${Token}">${Icon(Application.Mark)}<b>${Application.Title}</b><small>${Application.Caption} · ${Application.Opens}</small></div>
        ${ConstructApplicationChoices(Entry.Category, Token, false)}
        <button type="button" class="primary ow-launch" data-launch-selected>Open in ${Application.Title} ${Icon('arrow')}</button></section>`;
}
function ConstructOpenControls()
{
    const Token = QueryApplication(Selected);
    Query('#OpenAppSwitch').innerHTML = Object.entries(Applications).map(([Key,Application]) =>
        `<button type="button" role="radio" aria-checked="${Key===Token}" data-category="${Selected.Category}" data-application="${Key}" title="Open with ${Application.Title} · ${Application.Caption}">${Icon(Application.Mark)}${Application.Title}</button>`).join('');
    Query('#OpenEditorLabel').textContent = `Open in ${Applications[Token].Title}`;
}
function ConstructLibraryOpenWith()
{
    Query('#LibraryOpenWith').innerHTML = `<div class="pz-heading"><span class="pz-breadcrumbs">Inspector / Library</span><h2>Open with</h2><p>Choose the editor each format opens in. Cards and the reader follow this choice.</p></div>
        <section class="pz-card" data-card="Editors"><h3>${Icon('cube')}Editors</h3><div class="ow-app-list">${Object.values(Applications).map(Application =>
            `<div class="ow-app"><span class="ow-app-seat">${Icon(Application.Mark)}</span><div><strong>${Application.Title}</strong><small>${Application.Caption} · ${Application.Opens}</small></div></div>`).join('')}</div></section>
        <section class="pz-card" data-card="Format defaults"><h3>${Icon('grid')}Format defaults</h3>${['project','cad','geometry'].map(Category =>
            `<div class="ow-format"><span class="ow-format-name"><span class="format-dot ${Category}"></span>${FormatTitles[Category]}</span>${ConstructApplicationChoices(Category, OpenWith[Category], true)}</div>`).join('')}</section>`;
}
function LaunchApplication(Entry, Token)
{
    const Application = Applications[Token] || Applications.frontier;
    Query('#LaunchDescription').textContent = Token === 'solidarc'
        ? (Entry.Blank ? 'A blank project opens in SolidArc for modelling. Scenes and levels stay in Frontier.' : `${Entry.Title} opens in SolidArc. Its parts, sketches and assemblies load for modelling; scenes and levels stay in Frontier.`)
        : Entry.Blank ? 'A blank scene opens in Frontier. No other workspace is launched.' : Entry.Imported ? 'This is the selected local file for a future native hand-off.' : 'The Default Project opens in Frontier.';
    Query('#LaunchTarget').textContent = Token === 'frontier' && Entry.Category === 'document' ? 'Frontier · Document preview' : Application.Title;
    Query('#LaunchMark').innerHTML = `${Icon(Application.Mark)}<span>${Application.Title}</span>`;
    Query('#LaunchMark').dataset.application = Token;
    Query('#LaunchFile').textContent = Entry.Path;
    Query('#LaunchDialog').showModal();
}
let Selected = null;
let ActiveFilter = 'all';
let PreviewIndex = 0;
let ActiveRevision = 0;
let LiveRenderer = null;
let ThumbnailRenderer = null;
let SnapshotPending = false;
let SnapshotFailure = null;
const SnapshotCache = new Map();
const SnapshotJobs = new Map();
let DocumentEnabled = false;
let Zoom = 100;
let ActiveTab = 'details';
let ToastTimeout;
const Stars = new Set(RetrievePreference('frontier.prototype.browser.stars', [DefaultProjectToken]));
const LocalNotes = RetrievePreference('frontier.prototype.browser.notes', {});
const ObjectURLs = [];
const Theme = RetrievePreference('frontier.appearance.theme', 'oled');
function ConfigureTheme(Name)
{
    const Accepted = ['oled','light','dark'].includes(Name) ? Name : 'oled';
    document.documentElement.dataset.theme = Accepted;
    Query('#Theme').value = Accepted;
    PersistPreference('frontier.appearance.theme', Accepted);
}
function Notify(Message)
{
    Query('#Toast').textContent = Message;
    Query('#Toast').hidden = false;
    clearTimeout(ToastTimeout);
    ToastTimeout = setTimeout(() => Query('#Toast').hidden = true, 5000);
}
function ToggleStar(Token)
{
    Stars.has(Token) ? Stars.delete(Token) : Stars.add(Token);
    PersistPreference('frontier.prototype.browser.stars', [...Stars]);
    ConstructCatalogue();
    if (Selected) ConfigureStarButton();
}
function ConfigureStarButton()
{
    Query('#StarReader').setAttribute('aria-pressed', String(Stars.has(Selected.Token)));
    Query('#StarReader').style.color = Stars.has(Selected.Token) ? 'var(--accent)' : '';
    Query('#StarReader').setAttribute('aria-label', Stars.has(Selected.Token) ? 'Unstar file' : 'Star file');
}
function ConstructCatalogue()
{
    const Search = Query('#Search').value.trim().toLocaleLowerCase();
    const Entries = Catalogue.filter(Entry => (!Search || `${Entry.Title} ${Entry.Extension} ${Entry.Category}`.toLocaleLowerCase().includes(Search)) &&
        (ActiveFilter === 'all' || ActiveFilter === 'recent' || (ActiveFilter === 'starred' && Stars.has(Entry.Token)) || ActiveFilter === Entry.Category));
    if (Query('#Sort').value === 'name') Entries.sort((First, Second) => First.Title.localeCompare(Second.Title));
    Query('#AllCount').textContent = Catalogue.length;
    Query('#TallyProjects').textContent = Catalogue.filter(Entry => Entry.Category === 'project').length;
    Query('#TallyStarred').textContent = Catalogue.filter(Entry => Stars.has(Entry.Token)).length;
    Query('#CollectionCount').textContent = `${Entries.length} ${Entries.every(Entry => !Entry.Imported) ? 'project' : 'file'}${Entries.length === 1 ? '' : 's'}`;
    Query('#CollectionTitle').textContent = ({all:'Projects',recent:'Recently opened',starred:'Starred files',project:'Projects',cad:'CAD',geometry:'Geometry'})[ActiveFilter];
    Query('#Empty').hidden = Entries.length !== 0;
    Query('#Cards').innerHTML = Entries.map(Entry => `<article class="file-card"><button class="card-preview" data-open="${Escape(Entry.Token)}" aria-label="Preview ${Escape(Entry.Title)}">${ConstructThumbnail(Entry, 0, 0)}${Entry.Imported ? '<span class="preview-caption-tag">LOCAL FILE</span>' : ''}</button><button class="star-button" data-star="${Escape(Entry.Token)}" aria-label="${Stars.has(Entry.Token) ? 'Unstar' : 'Star'} ${Escape(Entry.Title)}" aria-pressed="${Stars.has(Entry.Token)}">${Icon('star')}</button><div class="card-text"><div class="card-title"><button data-open="${Escape(Entry.Token)}">${Escape(Entry.Title)}</button><span class="extension">${Escape(Entry.Extension)}</span></div><div class="card-subtitle">${Escape(Entry.Context)}</div><div class="card-meta"><span class="category-label"><span class="format-dot ${Entry.Category}"></span>${({project:'Project',cad:'CAD',geometry:'Geometry',document:'Document'})[Entry.Category]}</span><time>${Escape(Entry.Opened)}</time></div><div class="card-open" role="group" aria-label="Open ${Escape(Entry.Title)} with">${Object.entries(Applications).map(([Token,Application]) =>
        `<button type="button" class="${Token===QueryApplication(Entry) ? 'preferred' : ''}" data-launch="${Token}" data-token="${Escape(Entry.Token)}" title="Open in ${Application.Title} · ${Application.Caption}">${Icon(Application.Mark)}${Application.Title}</button>`).join('')}</div></div></article>`).join('');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FORMAT-SPECIFIC PREVIEW
//------------------------------------------------------------------------------------------------------------------------

function ActivateFile(Token)
{
    Selected = Catalogue.find(Entry => Entry.Token === Token);
    if (!Selected) return;
    PreviewIndex = 0;
    ActiveRevision = 0;
    Query('#RevisionSelect').innerHTML = Selected.Imported ? '<option value="0">Local file</option>' : Selected.Revisions.map((Revision, Index) => `<option value="${Index}">Version ${Selected.Revisions.length-Index}${Index===0 ? ' · Current' : ''}</option>`).join('');
    Query('#RevisionSelect').disabled = !!Selected.Imported;
    Query('#RevisionSelect').setAttribute('aria-label','Preview version');
    DocumentEnabled = Selected.Category === 'document';
    Zoom = 100;
    ActiveTab = 'details';
    Query('#Library').hidden = true;
    Query('#Reader').hidden = false;
    Query('#ReaderName').textContent = Selected.Title + Selected.Extension;
    Query('#ReaderFormat').textContent = Selected.Extension;
    Query('#RailTitle').textContent = ({project:'Levels',cad:Selected.Extension === '.sketch' ? 'Sketches' : 'Assembly',geometry:'Views',document:'Document'})[Selected.Category];
    Query('#RailCount').textContent = Selected.Views.length;
    Query('#Document').hidden = Selected.Category === 'document';
    Query('#ProjectSettingsPage').hidden = Selected.Category !== 'project';
    Query('#ProjectSetup').hidden = true;
    Query('.viewer-scroll').hidden = false;
    Query('#Document small').textContent = Selected.Imported ? 'Browser-generated summary' : 'Sample document';
    ConfigureStarButton();
    ConstructOpenControls();
    ConstructSequence();
    ConstructPreview();
    ConstructInspector();
    Query('#Back').focus();
}
function ConstructSequence()
{
    Query('#PreviewSequence').innerHTML = Selected.Views.map((Title, Index) => `<button class="preview-thumb" data-preview="${Index}" aria-current="${!DocumentEnabled && Index === PreviewIndex}">${ConstructMiniPage(Selected,Index)}<span><b>${Index+1}</b>${Escape(Title)}</span></button>`).join('');
    Query('#Document').setAttribute('aria-current',String(DocumentEnabled));
    Query('#ProjectSettingsPage').setAttribute('aria-current',String(!Query('#ProjectSetup').hidden));
}
// A document page in miniature: running header rule, title lines, the rendered figure, body lines (left rail + inspector).
function ConstructMiniPage(Entry,Index)
{
    return `<span class="mini-page"><span class="mini-page-head"></span><span class="mini-page-title"></span>${ConstructThumbnail(Entry,Index,ActiveRevision)}<span class="mini-page-lines"><i></i><i></i><i></i></span><span class="mini-page-foot"></span></span>`;
}
function QueryPageCount()
{
    return Selected.Category === 'document' ? 1 : Selected.Views.length + 1;
}
function ConstructPageChrome()
{
    // Running header / footer and the facts table printed on the page, like a PDF export of the project.
    const Version = Selected.Revisions.length-ActiveRevision;
    const Page = DocumentEnabled ? QueryPageCount() : PreviewIndex+1;
    Query('#PaperPage').classList.toggle('is-document',DocumentEnabled);
    Query('#PageHeadLeft').textContent = Selected.Title + Selected.Extension;
    Query('#PageHeadRight').textContent = Selected.Imported ? 'Local file' : `Version ${Version}${ActiveRevision===0 ? ' · Current' : ''}`;
    Query('#PageFootLeft').textContent = `${Applications[QueryApplication(Selected)].Title} · ${Selected.Path || 'Local selection'}`;
    Query('#PageFootNumber').textContent = `Page ${Page} of ${QueryPageCount()}`;
    Query('#PreviewIndex').textContent = `${Page} / ${QueryPageCount()}`;
    const Facts = [
        ['Format', Selected.Extension],
        [Query('#RailTitle').textContent, String(Selected.Views.length)],
        ['Version', Selected.Imported ? 'Local file' : `${Version} of ${Selected.Revisions.length}`],
        ['Opens in', Applications[QueryApplication(Selected)].Title],
        ['Last opened', Selected.Opened || '—']
    ];
    const Sheet=Query('#InspectorContent .detail-page');
    if (Sheet) Sheet.innerHTML=`${ConstructMiniPage(Selected,PreviewIndex)}<small>Page ${Page} of ${QueryPageCount()}</small>`;
    Query('#PageFacts').innerHTML = Facts.map(([Term,Detail]) => `<dt>${Escape(Term)}</dt><dd>${Escape(Detail)}</dd>`).join('');
}
function ConstructMarkdown(Text)
{
    // 📝 Render a deliberately small safe Markdown subset. Raw HTML and external/embedded resources are never executed.
    return Text.slice(0, 262144).split(/\n\s*\n/).map(Paragraph => {
        if (Paragraph.startsWith('# ')) return `<h1>${Escape(Paragraph.slice(2))}</h1>`;
        if (Paragraph.startsWith('## ')) return `<h2>${Escape(Paragraph.slice(3))}</h2>`;
        return `<p>${Escape(Paragraph)}</p>`;
    }).join('');
}
function ConstructPreview()
{
    const Title = DocumentEnabled ? (Selected.Category === 'document' ? Selected.Title : 'Overview.md') : Selected.Views[PreviewIndex];
    Query('#PreviewEyebrow').textContent = DocumentEnabled ? 'DOCUMENT' : `${Query('#RailTitle').textContent.toUpperCase()} ${String(PreviewIndex+1).padStart(2,'0')} · VERSION ${Selected.Revisions.length-ActiveRevision}`;
    Query('#PreviewTitle').textContent = Title;
    Query('#PreviewIndex').textContent = DocumentEnabled ? 'Document' : `${PreviewIndex+1} / ${Selected.Views.length}`;
    Query('#PreviewMode').textContent = Selected.Imported ? (Selected.ImageURL ? 'Local image' : Selected.Markdown ? 'Local Markdown' : 'Native preview not connected') : 'Reference WebGL 2';
    Query('#PreviewDescription').textContent = DocumentEnabled ? 'Markdown · read-only preview' : Selected.Description;
    Query('#PreviewBottom').textContent = Selected.Imported ? 'Your original file is unchanged' : `Sample · version ${Selected.Revisions.length-ActiveRevision}`;
    Query('#PreviewResolution').textContent = Selected.Imported ? 'Local file' : 'Drag to orbit · scroll to zoom';
    if(DocumentEnabled){Query('#PreviewMode').textContent='Markdown';Query('#PreviewResolution').textContent='A4 · 210 × 297 mm';}
    const Canvas=Query('#PreviewCanvas');Canvas.innerHTML='';Canvas.classList.toggle('document-canvas',DocumentEnabled);
    if (DocumentEnabled)
    {
        const Text=Selected.Markdown || `# ${Selected.Title}\n\n${Selected.Description}\n\n## Preview sequence\n\n${Selected.Views.map((Name,Index)=>`${Index+1}. ${Name}`).join('\n')}\n\n## Revision ${Selected.Revisions.length-ActiveRevision}\n\n${Selected.Revisions[ActiveRevision] || 'Local document'}\n\n## Preview\n\nThe image below is captured from the same WebGL renderer as the live review. Sample revisions are browser fixtures, not an on-disk history archive.`;
        Canvas.innerHTML=`<article class="document-page">${ConstructMarkdown(Text)}${Selected.Imported ? '' : ConstructThumbnail(Selected,0,ActiveRevision)}</article>`;
    }
    else if (Selected.Blank)
    {
        Canvas.innerHTML=`<div class="metadata-view">${Icon('plus')}<h3>${Escape(Selected.Title)}</h3><p>Blank scene. Open in Frontier to start building; no sample content is added.</p></div>`;
    }
    else if (Selected.Imported)
    {
        Canvas.innerHTML=Selected.ImageURL ? `<img class="artwork" src="${Escape(Selected.ImageURL)}" alt="Local image preview">` : `<div class="metadata-view">${Icon('file')}<h3>${Escape(Selected.Title+Selected.Extension)}</h3><p>This file was selected from your device. Native decoding is not connected.</p><span class="local-chip">${Escape(Selected.ByteCount.toLocaleString())} bytes · original unchanged</span></div>`;
    }
    else
    {
        try
        {
            if (!LiveRenderer)
            {
                LiveRenderer=new ReferencePreview(document.createElement('canvas'));
                let CaptureDelay;
                LiveRenderer.AttachNavigation(()=>{clearTimeout(CaptureDelay);CaptureDelay=setTimeout(CaptureCurrentPreview,140);});
                LiveRenderer.OnFailure=Message=>ConfigureRenderFailure(Message);
            }
            LiveRenderer.Configure(PreviewConfiguration(Selected,PreviewIndex,ActiveRevision));
            const Statistics=LiveRenderer.QueryStatistics();
            if (Title==='UV layout')
            {
                const UV=LiveRenderer.ConstructUV();UV.className='actual-uv';Canvas.append(UV);
                Query('#PreviewResolution').textContent='Generated cylindrical UV · actual vertices';
            }
            else if (Title==='Metadata')
            {
                Canvas.innerHTML=`<div class="metadata-view"><span class="eyebrow">RENDERED GEOMETRY · VERSION ${Selected.Revisions.length-ActiveRevision}</span><h3>${Escape(Selected.Title)}</h3><pre>${Escape(JSON.stringify(Statistics,null,2))}</pre></div>`;
            }
            else
            {
                const Surface=document.createElement('div');Surface.className='live-preview';
                LiveRenderer.Canvas.className='render-canvas';LiveRenderer.Canvas.setAttribute('aria-label','Live reference renderer. Drag to orbit, Shift-drag to pan, scroll to zoom, Home to reset.');Surface.append(LiveRenderer.Canvas);Canvas.append(Surface);
                const Controls=document.createElement('div');Controls.className='render-controls';Controls.innerHTML=`<label>View <select id="RenderChannel"><option value="0">Lit · EON / GGX</option><option value="5">CAD contours</option><option value="1">Albedo</option><option value="2">Specular</option><option value="3">Metallic</option><option value="4">Roughness</option></select></label><button id="CaptureThumbnail">Capture thumbnail</button><span>${Statistics.Triangles.toLocaleString()} triangles</span>`;Surface.append(Controls);
                Query('#RenderChannel').value=String(LiveRenderer.Configuration.Channel);
                Query('#RenderChannel').onchange=Event=>{LiveRenderer.Configuration.Channel=Number(Event.target.value);LiveRenderer.Refresh();CaptureCurrentPreview();};
                Query('#CaptureThumbnail').onclick=()=>{CaptureCurrentPreview();Notify('Thumbnail captured from the current rendered view.');};
                LiveRenderer.Refresh();
                requestAnimationFrame(()=>{if(!Canvas.contains(Surface))return;LiveRenderer.Refresh();CaptureCurrentPreview();});
            }
        }
        catch(Failure){ConfigureRenderFailure(Failure.message);}
    }
    Zoom=100;
    ConstructPageChrome();
    ConfigurePaperScale(100);
    ConstructSequence();
    Query('#Previous').disabled=QueryPageCount()<=1;
    Query('#Next').disabled=QueryPageCount()<=1;
}
function ConfigureRenderFailure(Message)
{
    Query('#PreviewCanvas').innerHTML=`<div class="render-failure"><strong>Rendered preview unavailable</strong><p>${Escape(Message)}</p><p>No illustration has been substituted.</p></div>`;
    Query('#PreviewMode').textContent='Renderer unavailable';
}
function ConfigureZoom(Amount)
{
    Zoom = Math.max(50, Math.min(200, Amount));
    Query('#ZoomReset').textContent = Zoom+'%';
    // The zoom scales the whole sheet, like a PDF viewer; the live figure re-renders once at the new size.
    ConfigurePaperScale(Zoom);
    if (LiveRenderer && Query('#PreviewCanvas').contains(LiveRenderer.Canvas)) requestAnimationFrame(()=>LiveRenderer.Refresh());
}
function QueryNotes()
{
    return [...(Selected.Notes || []).map(Note => ({Author:Note[0],Initial:Note[1],Text:Note[2],Time:Note[3],Sample:true})), ...(Array.isArray(LocalNotes[Selected.Token]) ? LocalNotes[Selected.Token] : [])];
}
function ConstructInspector()
{
    document.querySelectorAll('[data-tab]').forEach(Button => Button.setAttribute('aria-selected', String(Button.dataset.tab === ActiveTab)));
    Query('#CommentCount').textContent = QueryNotes().length || '';
    Query('#CommentForm').hidden = ActiveTab !== 'comments';
    const Surface = Query('#InspectorContent');
    if (ActiveTab === 'details')
    {
        Surface.innerHTML = `<div class="pz-heading"><span class="pz-breadcrumbs">Inspector / ${Escape(FormatTitles[Selected.Category] || 'Files')}</span><h2>${Escape(Selected.Title)}</h2><p>${Escape(Selected.Extension)} · ${Selected.Imported ? 'Selected from your device' : 'Sample file · local workspace'}</p></div><div class="detail-hero detail-page">${ConstructMiniPage(Selected,PreviewIndex)}<small>Page ${DocumentEnabled ? QueryPageCount() : PreviewIndex+1} of ${QueryPageCount()}</small></div>${ConstructOpenWithCard(Selected)}<section class="pz-card" data-card="Details"><h3>${Icon('file')}Details</h3><p class="pz-card-text">${Escape(Selected.Description)}</p><div class="detail-list"><span>Workspace</span><strong>${({project:'Game project',cad:'Drafting / CAD',geometry:'Geometry',document:'Document'})[Selected.Category]}</strong><span>${Selected.Category === 'project' ? 'Levels' : Selected.Category === 'cad' ? 'Assemblies / sketches' : 'Preview views'}</span><strong>${Selected.Views.length}</strong><span>Last opened</span><strong>${Escape(Selected.Opened)}</strong><span>Storage</span><strong>${Selected.Imported ? 'Local file selection' : 'Sample location'}</strong></div></section><section class="pz-card" data-card="Location"><h3>${Icon('folder')}Location</h3><div class="source-path">${Escape(Selected.Path)}</div><span class="local-chip">${Selected.Imported ? 'Read-only · native decoder pending' : 'Sample file · read-only'}</span></section>`;
    }
    else if (ActiveTab === 'comments')
    {
        const Notes = QueryNotes();
        Surface.innerHTML = `<div class="comment-context">FILE NOTES · ${Notes.some(Note => Note.Sample) ? 'SAMPLE THREAD + YOUR LOCAL NOTES' : 'BROWSER-LOCAL ONLY'}</div>` + (Notes.length ? Notes.map(Note => `<article class="comment"><span class="comment-avatar">${Escape(Note.Initial)}</span><div><h4>${Escape(Note.Author)}${Note.Sample ? ' · sample' : ' · you'}</h4><time>${Escape(Note.Time)}</time><p>${Escape(Note.Text)}</p></div></article>`).join('') : '<p>No notes yet. Add the first thought below.</p>');
    }
    else
    {
        Surface.innerHTML = '<div class="comment-context">'+(Selected.Imported ? 'NO NATIVE HISTORY CONNECTED' : 'SAMPLE VERSION HISTORY · READ-ONLY')+'</div>' + (!Selected.Imported ? Selected.Revisions.map((Revision, Index) => `<button type="button" data-revision="${Index}" aria-pressed="${ActiveRevision===Index}" class="revision ${Index === 0 ? 'current' : ''} ${ActiveRevision===Index ? 'selected-revision' : ''}"><strong>Version ${Selected.Revisions.length-Index}</strong><small>${Index === 0 ? 'Today' : Index === 1 ? 'October 15, 2026' : 'October 12, 2026'} · Sample revision</small><p>${Escape(Revision)}</p>${ConstructThumbnail(Selected,0,Index)}</button>`).join('') : '<p>The native history archive has not been read. No revisions have been invented for this file.</p>') + '<span class="local-chip">Select a version to review it · restoring disk files is not connected</span>';
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LOCAL FILE ACCESS AND EVENTS
//------------------------------------------------------------------------------------------------------------------------

async function AttachFiles(Files)
{
    let Attached = 0;
    for (const File of [...Files].slice(0,200))
    {
        const Extension = '.'+(File.name.split('.').pop() || '').toLowerCase();
        const Category = ['.project','.projectspace','.scene'].includes(Extension) ? 'project' : ['.assembly','.sketch'].includes(Extension) ? 'cad' : ['.geometry','.geometryspace','.png','.jpg','.jpeg','.webp'].includes(Extension) ? 'geometry' : Extension === '.md' ? 'document' : null;
        if (!Category) continue;
        if (File.size > 100 * 1024 * 1024) { Notify('Files over 100 MiB are not loaded by this design prototype.'); continue; }
        const Token = 'local-'+Date.now()+'-'+Attached;
        const Entry = {Token,Title:File.name.slice(0,-Extension.length),Extension,Category,Imported:true,Description:'A local file selected for a read-only browser preview.',Art:Category === 'cad' ? 'Assembly' : Category === 'project' ? 'Valley' : 'Geometry',Context:'Local file · read-only',Opened:'Just now',Path:File.webkitRelativePath || File.name,ByteCount:File.size,Views:[Category === 'project' ? 'Project preview' : Category === 'cad' ? 'CAD preview' : Category === 'document' ? 'Document' : 'Geometry preview'],Notes:[],Revisions:[]};
        if (['.png','.jpg','.jpeg','.webp'].includes(Extension)) { Entry.ImageURL = URL.createObjectURL(File); ObjectURLs.push(Entry.ImageURL); Entry.Views=['Image']; }
        if (Extension === '.md') { if (File.size > 262144) { Notify('Markdown preview is limited to 256 KiB.'); continue; } Entry.Markdown = await File.text(); }
        if (Extension === '.projectspace' && File.size <= 10 * 1024 * 1024)
        {
            try { Entry.BrowserSettings = ParseProjectToml(await File.text()); Entry.Title = Entry.BrowserSettings.project?.name || Entry.Title; }
            catch (Failure) { console.info('Project manifest preview only:',Failure.message); }
        }
        Catalogue.unshift(Entry); Attached++;
    }
    ActiveFilter = 'all'; Query('#Search').value = '';
    document.querySelectorAll('[data-filter]').forEach(Button => Button.classList.toggle('active',Button.dataset.filter === 'all'));
    ConstructCatalogue();
    if (Attached === 1) ActivateFile(Catalogue[0].Token);
    Notify(Attached ? `${Attached} local file${Attached > 1 ? 's' : ''} attached. Absolute paths are hidden by your browser; native project decoding is not connected.` : 'No supported files found. Choose .project, .assembly, .sketch, geometry, Markdown or image files.');
}
function ReturnLibrary()
{
    if (!Query('#ProjectSetup').hidden) CloseProjectSettingsPage();
    Query('#LibraryThemeSlot').append(Query('#ThemeControl'));
    Query('#Library').hidden = false; Query('#Reader').hidden = true; Selected = null; ConstructCatalogue(); Query('#Browse').focus();
}
Query('#Theme').addEventListener('change', Event => ConfigureTheme(Event.target.value));
Query('#Browse').onclick = Query('#BrowseRail').onclick = () => Query('#FilePicker').click();
Query('#BrowseFolder').onclick = () => Query('#FolderPicker').click();
Query('#FilePicker').onchange = async Event => { await AttachFiles(Event.target.files); Event.target.value=''; };
Query('#FolderPicker').onchange = async Event => { await AttachFiles(Event.target.files); Event.target.value=''; };
Query('#Back').onclick = ReturnLibrary;
Query('#Search').oninput = Query('#Sort').onchange = ConstructCatalogue;
Query('#ClearSearch').onclick = () => { Query('#Search').value=''; ConstructCatalogue(); };
document.querySelectorAll('[data-filter]').forEach(Button => Button.onclick = () => { ActiveFilter=Button.dataset.filter; document.querySelectorAll('[data-filter]').forEach(Other => Other.classList.toggle('active',Other===Button)); ConstructCatalogue(); });
Query('#Cards').onclick = Event => { const Star = Event.target.closest('[data-star]'); const Launch = Event.target.closest('[data-launch]'); const Open = Event.target.closest('[data-open]'); if (Star) ToggleStar(Star.dataset.star); else if (Launch) LaunchApplication(Catalogue.find(Entry => Entry.Token === Launch.dataset.token), Launch.dataset.launch); else if (Open) ActivateFile(Open.dataset.open); };
for (const Surface of ['#LibraryOpenWith','#OpenAppSwitch','#InspectorContent'])
    Query(Surface).addEventListener('click', Event => {
        const Choice = Event.target.closest('[data-application]');
        if (Choice) { ConfigureApplication(Choice.dataset.category, Choice.dataset.application); return; }
        if (Event.target.closest('[data-launch-selected]') && Selected) LaunchApplication(Selected, QueryApplication(Selected));
    });
Query('#PreviewSequence').onclick = Event => { const Button=Event.target.closest('[data-preview]'); if (Button) { if (!Query('#ProjectSetup').hidden) CloseProjectSettingsPage(); PreviewIndex=Number(Button.dataset.preview);DocumentEnabled=false;ConstructPreview(); } };
Query('#Document').onclick = () => { if (!Query('#ProjectSetup').hidden) CloseProjectSettingsPage(); Query('.viewer-scroll').hidden=false;DocumentEnabled=true;ConstructPreview(); };
Query('#ProjectSettingsPage').onclick = () => OpenProjectSettingsPage(Selected);
Query('#BackToOverview').onclick = CloseProjectSettingsPage;
function ConfigurePage(Page)
{
    if (Selected.Category === 'document') return;
    if (!Query('#ProjectSetup').hidden) CloseProjectSettingsPage();
    const Count=QueryPageCount();
    const Wrapped=((Page%Count)+Count)%Count;
    DocumentEnabled=Wrapped===Count-1;
    if(!DocumentEnabled)PreviewIndex=Wrapped;
    ConstructPreview();
}
Query('#Previous').onclick = () => ConfigurePage((DocumentEnabled ? QueryPageCount()-1 : PreviewIndex)-1);
Query('#Next').onclick = () => ConfigurePage((DocumentEnabled ? QueryPageCount()-1 : PreviewIndex)+1);
Query('#ZoomIn').onclick = () => ConfigureZoom(Zoom+25);
Query('#ZoomOut').onclick = () => ConfigureZoom(Zoom-25);
Query('#ZoomReset').onclick = Query('#Fit').onclick = () => ConfigureZoom(100);
Query('#StarReader').onclick = () => ToggleStar(Selected.Token);
document.querySelectorAll('[data-tab]').forEach(Button => Button.onclick = () => { ActiveTab=Button.dataset.tab;ConstructInspector(); });
Query('#InspectorContent').addEventListener('click',Event=>{const Button=Event.target.closest('[data-revision]');if(Button) ConfigureRevision(Number(Button.dataset.revision));});
Query('#RevisionSelect').onchange=Event=>ConfigureRevision(Number(Event.target.value));
Query('#CommentForm').onsubmit = Event => { Event.preventDefault();const Text=Query('#CommentText').value.trim();if (!Text) return; if (!Array.isArray(LocalNotes[Selected.Token])) LocalNotes[Selected.Token]=[];LocalNotes[Selected.Token].push({Author:'You',Initial:'Y',Text:Text.slice(0,2000),Time:'Just now',Sample:false});PersistPreference('frontier.prototype.browser.notes',LocalNotes);Query('#CommentText').value='';ConstructInspector();Notify('Note saved in this browser. It has not been written to a project file.'); };
Query('#OpenEditor').onclick = () => LaunchApplication(Selected, QueryApplication(Selected));
function ConfigureOpenMenu(Open)
{
    Query('#OpenMenu').hidden=!Open;
    Query('#OpenMenuToggle').setAttribute('aria-expanded',String(Open));
}
Query('#OpenMenuToggle').onclick = Event => { Event.stopPropagation(); ConfigureOpenMenu(Query('#OpenMenu').hidden); };
Query('#OpenMenu').addEventListener('click', Event => { if (Event.target.closest('[data-application]')) setTimeout(()=>{ConfigureOpenMenu(false);ConstructPageChrome();},0); });
document.addEventListener('click', Event => { if (!Event.target.closest('.open-split')) ConfigureOpenMenu(false); });
// Escape closes the open-with menu first; only a second Escape leaves the reader.
document.addEventListener('keydown', Event => { if (Event.key==='Escape' && !Query('#OpenMenu').hidden) { ConfigureOpenMenu(false); Event.stopImmediatePropagation(); } }, true);
Query('#NewProject').onclick = () => ActivateFile(CreateProject().Token);
Query('#CloseDialog').onclick = Query('#ReturnPreview').onclick = () => Query('#LaunchDialog').close();
document.addEventListener('keydown', Event => { if (Event.target.tagName==='CANVAS' || ['INPUT','TEXTAREA','SELECT'].includes(Event.target.tagName) || Query('#LaunchDialog').open) return;if (Event.key==='/' && !Query('#Library').hidden) { Event.preventDefault();Query('#Search').focus(); } if (Event.key==='Escape' && !Query('#ProjectSetup').hidden) CloseProjectSettingsPage();else if (Event.key==='Escape' && !Query('#Reader').hidden) ReturnLibrary();if (Selected && Event.key==='ArrowRight') Query('#Next').click();if (Selected && Event.key==='ArrowLeft') Query('#Previous').click(); });
window.addEventListener('beforeunload', () => ObjectURLs.forEach(URLToReclaim => URL.revokeObjectURL(URLToReclaim)));

//------------------------------------------------------------------------------------------------------------------------
//                                                     RENDERED SNAPSHOTS AND VERSION SELECTION
//------------------------------------------------------------------------------------------------------------------------

function PreviewConfiguration(Entry,Index,Revision)
{
    const Title=Entry.Views[Index] || '';
    return {Category:Entry.Category,View:Index,Revision,Sketch:Entry.Extension==='.sketch'||Title.includes('Sketch'),
            Channel:Title==='Texture' ? 1 : Entry.Category==='cad' ? 5 : 0,HiddenEdges:Entry.Category==='cad'};
}
function SnapshotKey(Entry,Index,Revision){return `${Entry.Token}:${Revision}:${Index}`;}
function ConstructThumbnail(Entry,Index,Revision)
{
    if(Entry.Blank)return `<span class="thumb-art">${Icon('plus')}</span>`;
    if(Entry.Imported)return Entry.ImageURL ? `<img src="${Escape(Entry.ImageURL)}" alt="Local image preview">` : `<span class="thumb-art">${Icon('file')}</span>`;
    const Key=SnapshotKey(Entry,Index,Revision),URI=SnapshotCache.get(Key);
    if(!URI&&!SnapshotFailure){SnapshotJobs.set(Key,{Entry,Index,Revision});QueueSnapshots();}
    return `<span class="rendered-thumbnail" data-snapshot="${Escape(Key)}">${URI ? `<img src="${URI}" alt="Rendered ${Escape(Entry.Views[Index])}, version ${Entry.Revisions.length-Revision}">` : `<span class="render-pending">${SnapshotFailure ? 'WebGL unavailable' : 'Rendering preview…'}</span>`}</span>`;
}
function ApplySnapshot(Key,URI)
{
    SnapshotCache.set(Key,URI);
    document.querySelectorAll('[data-snapshot]').forEach(Surface=>{if(Surface.dataset.snapshot===Key)Surface.innerHTML=`<img src="${URI}" alt="Captured rendered preview">`;});
}
function QueueSnapshots()
{
    if(SnapshotPending)return;
    SnapshotPending=true;
    requestAnimationFrame(()=>{
        SnapshotPending=false;
        const Pending=SnapshotJobs.entries().next();if(Pending.done||SnapshotFailure)return;
        const [Key,{Entry,Index,Revision}]=Pending.value;SnapshotJobs.delete(Key);
        if(!SnapshotCache.has(Key))try
        {
            if(!ThumbnailRenderer)ThumbnailRenderer=new ReferencePreview(document.createElement('canvas'));
            ThumbnailRenderer.Configure(PreviewConfiguration(Entry,Index,Revision));
            const URI=Entry.Views[Index]==='UV layout' ? ThumbnailRenderer.ConstructUV(384,246).toDataURL('image/png') : ThumbnailRenderer.Capture();
            ApplySnapshot(Key,URI);
        }
        catch(Failure)
        {
            SnapshotFailure=Failure.message;
            document.querySelectorAll('.render-pending').forEach(Surface=>Surface.textContent='WebGL unavailable');
            console.warn(Failure.message);
        }
        if(SnapshotJobs.size&&!SnapshotFailure)QueueSnapshots();
    });
}
function CaptureCurrentPreview()
{
    if(!Selected||Selected.Imported||DocumentEnabled||!LiveRenderer||!Query('#PreviewCanvas').contains(LiveRenderer.Canvas))return;
    try{ApplySnapshot(SnapshotKey(Selected,PreviewIndex,ActiveRevision),LiveRenderer.Canvas.toDataURL('image/png'));}
    catch(Failure){Notify('Thumbnail capture failed: '+Failure.message);}
}
function ConfigureRevision(Index)
{
    if(!Selected||Selected.Imported||Index<0||Index>=Selected.Revisions.length)return;
    ActiveRevision=Index;Query('#RevisionSelect').value=String(Index);
    ConstructPreview();ConstructInspector();
}
ConfigureTheme(Theme);
ConstructCatalogue();
ConstructLibraryOpenWith();

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROJECT DETAILS AND TOML CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

let SetupEntry = null;
let SetupIconUrl = null;
let SetupDirty = false;
const SetupFields = {
    ProjectName:'#ProjectName', ProjectId:'#ProjectId', ProjectVersion:'#ProjectVersion', ProjectIcon:'#ProjectIconPath',
    RenderWidth:'#RenderWidth', RenderHeight:'#RenderHeight', RenderFormat:'#RenderFormat',
    TermsVersion:'#TermsVersion', TermsText:'#TermsText', TermsRequired:'#TermsRequired',
    EOSEnabled:'#EOSEnabled', EOSProductId:'#EOSProductId', EOSSandboxId:'#EOSSandboxId', EOSDeploymentId:'#EOSDeploymentId', EOSClientId:'#EOSClientId',
    PhotonEnabled:'#PhotonEnabled', PhotonAppId:'#PhotonAppId', PhotonAppVersion:'#PhotonAppVersion', PhotonRoom:'#PhotonRoom', PhotonRegion:'#PhotonRegion',
    EOSClientSecret:'#EOSClientSecret', EOSEncryptionKey:'#EOSEncryptionKey', PhotonServerSecret:'#PhotonServerSecret'
};
function MarkSetupDirty()
{
    SetupDirty = true;
    Query('#ConfigStatus').textContent = 'Unsaved changes on this .projectspace page.';
}
function OpenProjectSettingsPage(Entry)
{
    SetupEntry = Entry || Selected;
    if (!SetupEntry || SetupEntry.Category !== 'project') return;
    Selected = SetupEntry;
    Query('#Library').hidden = true;
    Query('#Reader').hidden = false;
    Query('#ProjectFileLabel').textContent = SetupEntry.Path.split('/').pop();
    Query('.viewer-scroll').hidden = true;
    Query('#ProjectSetup').hidden = false;
    Query('#ProjectSettingsPage').hidden = false;
    Query('#ProjectSettingsPage').setAttribute('aria-current','true');
    Query('#ProjectSetup').scrollTop = 0;
    window.scrollTo(0,0);
    const Saved = SetupEntry.DraftSettings || SetupEntry.BrowserSettings;
    if (Saved) ApplyProjectSettings(Saved, false);
    else ApplyProjectSettings({project:{name:SetupEntry.Title,id:CreateProjectId(),version:'',icon:''},render:{width:1920,height:1080,format:'png'},terms:{version:'',require_acceptance:false,text:''},eos:{enabled:true},photon:{enabled:false,app_version:'1.0',room:'FrontierDevRoom',region:'eu'}}, false);
    Query('#ReaderName').textContent = SetupEntry.Title + SetupEntry.Extension;
    UpdateProjectIconPreview();
    ConfigureServiceFields();
    Query('#BackToOverview').focus();
}
function CloseProjectSettingsPage()
{
    Query('#ProjectSetup').hidden = true;
    Query('.viewer-scroll').hidden = false;
    if (SetupDirty && SetupEntry) SetupEntry.DraftSettings = CollectProjectSettings(false);
    Query('#ProjectSettingsPage').setAttribute('aria-current','false');
    if (Selected) Query('#Back').focus();
}

function ConfigureServiceFields()
{
    Query('#EOSFields').classList.toggle('is-disabled',!Query('#EOSEnabled').checked);
    Query('#PhotonFields').classList.toggle('is-disabled',!Query('#PhotonEnabled').checked);
}
function UpdateProjectIconPreview()
{
    const Preview = Query('#ProjectIconPreview');
    Preview.textContent = '';
    Preview.innerHTML = SetupIconUrl ? `<img src="${Escape(SetupIconUrl)}" alt="Selected project icon preview">` : Escape((Query('#ProjectName').value.trim() || 'F').slice(0,1).toUpperCase());
}
function CreateProjectId()
{
    if (globalThis.crypto?.randomUUID) return crypto.randomUUID();
    return 'frontier-'+Date.now().toString(36)+'-'+Math.random().toString(36).slice(2,10);
}
function TomlString(Value)
{
    return JSON.stringify(String(Value ?? ''));
}
function CollectProjectSettings(IncludeSecrets)
{
    if (!Query('#ProjectId').value.trim()) Query('#ProjectId').value = CreateProjectId();
    const Settings = {
        project:{name:Query('#ProjectName').value.trim(),id:Query('#ProjectId').value.trim(),version:Query('#ProjectVersion').value.trim(),icon:Query('#ProjectIconPath').value.trim()},
        render:{width:Number(Query('#RenderWidth').value),height:Number(Query('#RenderHeight').value),format:Query('#RenderFormat').value},
        terms:{version:Query('#TermsVersion').value.trim(),require_acceptance:Query('#TermsRequired').checked,text:Query('#TermsText').value},
        eos:{enabled:Query('#EOSEnabled').checked,product_id:Query('#EOSProductId').value.trim(),sandbox_id:Query('#EOSSandboxId').value.trim(),deployment_id:Query('#EOSDeploymentId').value.trim(),client_id:Query('#EOSClientId').value.trim()},
        photon:{enabled:Query('#PhotonEnabled').checked,app_id:Query('#PhotonAppId').value.trim(),app_version:Query('#PhotonAppVersion').value.trim(),room:Query('#PhotonRoom').value.trim(),region:Query('#PhotonRegion').value},
        local_secrets:{eos:{client_secret:Query('#EOSClientSecret').value,encryption_key:Query('#EOSEncryptionKey').value},photon:{server_secret:Query('#PhotonServerSecret').value}}
    };
    if (!IncludeSecrets) delete Settings.local_secrets;
    return Settings;
}
function SerializeProjectSpace(Settings)
{
    const ProjectName = Settings.project.name || 'Untitled';
    const BrowserConfigurationJson = JSON.stringify(Settings);
    return [
        '# Frontier .ProjectSpace container · generated by Project Browser',
        '[".DocumentHeader"]',
        'ContainerExtension = ".ProjectSpace"',
        'SchemaVersion = 1',
        `DocumentTitle = ${TomlString(ProjectName)}`,
        'AuthoringWorkspace = "Frontier.ProjectWorkspace"',
        'BinaryAlignmentBytes = 64',
        '',
        '[".ProjectSettings"]',
        `ProjectTitle = ${TomlString(ProjectName)}`,
        'StartupSpacePath = "Main.UVSpace"',
        'TargetTickRateHz = 120',
        `BrowserConfigurationJson = ${TomlString(BrowserConfigurationJson)}`,
        '',
        '# === FRONTIER_BINARY_BOUNDARY ===',
        ''
    ].join('\n');
}
function SerializeProjectToml(Settings)
{
    const Lines = [
        '# Local-only Frontier configuration for development; not a .projectspace container.',
        '[project]',`name = ${TomlString(Settings.project.name)}`,`id = ${TomlString(Settings.project.id)}`,`version = ${TomlString(Settings.project.version)}`,`icon = ${TomlString(Settings.project.icon)}`,'',
        '[render]',`width = ${Settings.render.width}`,`height = ${Settings.render.height}`,`format = ${TomlString(Settings.render.format)}`,'',
        '[terms]',`version = ${TomlString(Settings.terms.version)}`,`require_acceptance = ${Settings.terms.require_acceptance}`,`text = ${TomlString(Settings.terms.text)}`,'',
        '[eos]',`enabled = ${Settings.eos.enabled}`,`product_id = ${TomlString(Settings.eos.product_id)}`,`sandbox_id = ${TomlString(Settings.eos.sandbox_id)}`,`deployment_id = ${TomlString(Settings.eos.deployment_id)}`,`client_id = ${TomlString(Settings.eos.client_id)}`,'',
        '[photon]',`enabled = ${Settings.photon.enabled}`,`app_id = ${TomlString(Settings.photon.app_id)}`,`app_version = ${TomlString(Settings.photon.app_version)}`,`room = ${TomlString(Settings.photon.room)}`,`region = ${TomlString(Settings.photon.region)}`
    ];
    if (Settings.local_secrets)
    {
        Lines.push('','[local_secrets.eos]',`client_secret = ${TomlString(Settings.local_secrets.eos.client_secret)}`,`encryption_key = ${TomlString(Settings.local_secrets.eos.encryption_key)}`,'','[local_secrets.photon]',`server_secret = ${TomlString(Settings.local_secrets.photon.server_secret)}`);
    }
    return Lines.join('\n')+'\n';
}
function ParseProjectToml(Text)
{
    const Root = {};
    let Section = Root;
    const ManifestText = Text.split('\n# === FRONTIER_BINARY_BOUNDARY ===')[0];
    for (const [Index,RawLine] of ManifestText.split(/\r?\n/).entries())
    {
        const Line = RawLine.trim();
        if (!Line || Line.startsWith('#')) continue;
        const Header = Line.match(/^\[(?:"([^"]+)"|([a-zA-Z0-9_.-]+))\]$/);
        if (Header)
        {
            const Name = Header[1] || Header[2];
            if (Name.startsWith('.')) Section = Root[Name] ||= {};
            else { Section = Root; for (const Part of Name.split('.')) Section = Section[Part] ||= {}; }
            continue;
        }
        const Match = Line.match(/^([a-zA-Z0-9_-]+)\s*=\s*(.*)$/);
        if (!Match) throw new Error(`Invalid TOML manifest at line ${Index+1}`);
        const [,Key,Literal] = Match;
        let Value;
        if (Literal.startsWith('"')) Value = JSON.parse(Literal);
        else if (Literal === 'true' || Literal === 'false') Value = Literal === 'true';
        else if (/^-?(?:\d+\.?\d*|\.\d+)$/.test(Literal)) Value = Number(Literal);
        else throw new Error(`Unsupported TOML value at line ${Index+1}`);
        Section[Key] = Value;
    }
    const ProjectSpace = Root['.ProjectSettings'];
    if (ProjectSpace)
    {
        if (ProjectSpace.BrowserConfigurationJson)
        {
            const Settings = JSON.parse(ProjectSpace.BrowserConfigurationJson);
            Settings.project ||= {};
            Settings.project.name ||= ProjectSpace.ProjectTitle || Root['.DocumentHeader']?.DocumentTitle || 'Untitled';
            return Settings;
        }
        const Name = ProjectSpace.ProjectTitle || Root['.DocumentHeader']?.DocumentTitle || 'Untitled';
        return {project:{name:Name,id:'',version:'',icon:''},render:{width:1920,height:1080,format:'png'},terms:{version:'',require_acceptance:false,text:''},eos:{enabled:false},photon:{enabled:false}};
    }
    if (!Root.project || !Root.render) throw new Error('Expected a Frontier .projectspace manifest.');
    return Root;
}
function ApplyProjectSettings(Settings, UpdateStatus = true)
{
    const Values = {
        ProjectName:Settings.project?.name ?? SetupEntry?.Title ?? 'Untitled', ProjectId:Settings.project?.id ?? '', ProjectVersion:Settings.project?.version ?? '', ProjectIcon:Settings.project?.icon ?? '',
        RenderWidth:Settings.render?.width ?? 1920, RenderHeight:Settings.render?.height ?? 1080, RenderFormat:Settings.render?.format ?? 'png',
        TermsVersion:Settings.terms?.version ?? '', TermsText:Settings.terms?.text ?? '', TermsRequired:Settings.terms?.require_acceptance ?? false,
        EOSEnabled:Settings.eos?.enabled ?? false, EOSProductId:Settings.eos?.product_id ?? '', EOSSandboxId:Settings.eos?.sandbox_id ?? '', EOSDeploymentId:Settings.eos?.deployment_id ?? '', EOSClientId:Settings.eos?.client_id ?? '',
        PhotonEnabled:Settings.photon?.enabled ?? false, PhotonAppId:Settings.photon?.app_id ?? '', PhotonAppVersion:Settings.photon?.app_version ?? '1.0', PhotonRoom:Settings.photon?.room ?? 'FrontierDevRoom', PhotonRegion:Settings.photon?.region ?? 'eu'
    };
    for (const [Key,Selector] of Object.entries(SetupFields))
    {
        const Element = Query(Selector);
        const Value = Values[Key];
        if (Key.endsWith('Secret') || Key === 'EOSEncryptionKey') Element.value = '';
        else if (Value !== undefined && Value !== null) Element.type === 'checkbox' ? Element.checked = !!Value : Element.value = String(Value);
    }
    Query('#EOSClientSecret').value = Settings.local_secrets?.eos?.client_secret || '';
    Query('#EOSEncryptionKey').value = Settings.local_secrets?.eos?.encryption_key || '';
    Query('#PhotonServerSecret').value = Settings.local_secrets?.photon?.server_secret || '';
    ConfigureServiceFields();
    UpdateProjectIconPreview();
    SetupDirty = false;
    if (UpdateStatus) Query('#ConfigStatus').textContent = Settings.local_secrets ? 'Local test data loaded. Secrets remain in this tab.' : 'Project settings loaded from .projectspace.';
    if (SetupEntry && Settings.project?.name) SetupEntry.Title = String(Settings.project.name);
}
async function SaveProjectSettings(IncludeSecrets)
{
    if (!Query('#ProjectSetupForm').reportValidity()) return;
    if (IncludeSecrets && !window.confirm('This writes secret values as plaintext into a local TOML file. Keep it out of source control and packages. Continue?')) return;
    const Settings = CollectProjectSettings(IncludeSecrets);
    const FileName = IncludeSecrets ? 'ProjectSettings.local.toml' : (SetupEntry?.Path.split('/').pop() || `${Settings.project.name.replace(/[^a-zA-Z0-9_-]/g,'_')}.projectspace`);
    const Content = IncludeSecrets ? SerializeProjectToml(Settings) : SerializeProjectSpace(Settings);
    try
    {
        if (window.showSaveFilePicker)
        {
            const Extension = IncludeSecrets ? '.toml' : '.projectspace';
            const Handle = await window.showSaveFilePicker({suggestedName:FileName,types:[{description:IncludeSecrets?'Local test configuration':'Frontier project space',accept:{'application/octet-stream':[Extension]}}]});
            const Writable = await Handle.createWritable();
            await Writable.write(Content);
            await Writable.close();
            if (!IncludeSecrets) SetupEntry.Path = Handle.name;
        }
        else
        {
            const Url = URL.createObjectURL(new Blob([Content],{type:IncludeSecrets?'text/plain;charset=utf-8':'application/octet-stream'}));
            const Link = document.createElement('a'); Link.href=Url; Link.download=FileName; Link.click();
            setTimeout(()=>URL.revokeObjectURL(Url),1000);
        }
        SetupDirty = false;
        if (!IncludeSecrets) { SetupEntry.BrowserSettings = Settings; SetupEntry.DraftSettings = null; SetupEntry.Title = Settings.project.name; Query('#ReaderName').textContent = SetupEntry.Title + SetupEntry.Extension; Query('#ProjectFileLabel').textContent = SetupEntry.Path.split('/').pop(); ConstructCatalogue(); }
        Query('#ConfigStatus').textContent = IncludeSecrets ? 'Local test TOML saved separately. It contains plaintext secrets; do not commit or package it.' : 'Project settings saved in the selected .projectspace manifest. Local secrets were excluded.';
        Notify(IncludeSecrets ? 'Local test TOML saved separately. Keep it private.' : 'Project settings saved to .projectspace without local secrets.');
    }
    catch(Failure)
    {
        if (Failure.name === 'AbortError') return;
        Query('#ConfigStatus').textContent = 'Save failed: '+Failure.message;
        Notify('Could not save project settings: '+Failure.message);
    }
}
Query('#ProjectSetupForm').addEventListener('submit',Event=>Event.preventDefault());
for (const Selector of Object.values(SetupFields)) Query(Selector).addEventListener('input',()=>{MarkSetupDirty();if(Selector==='#ProjectName')UpdateProjectIconPreview();});
for (const Selector of ['#TermsRequired','#EOSEnabled','#PhotonEnabled','#RenderFormat','#PhotonRegion']) Query(Selector).addEventListener('change',()=>{MarkSetupDirty();ConfigureServiceFields();});
Query('#RenderPreset').onchange = Event => {
    const [Width,Height] = Event.target.value.split('x');
    if (Width && Height) { Query('#RenderWidth').value=Width; Query('#RenderHeight').value=Height; }
    MarkSetupDirty();
};
Query('#RenderWidth').oninput = Query('#RenderHeight').oninput = () => { Query('#RenderPreset').value='custom'; MarkSetupDirty(); };
Query('#SaveProjectConfig').onclick = () => SaveProjectSettings(false);
Query('#SaveLocalTestConfig').onclick = () => SaveProjectSettings(true);
Query('#LoadProjectConfig').onclick = () => Query('#ProjectConfigPicker').click();
Query('#ProjectConfigPicker').onchange = async Event => {
    const File = Event.target.files?.[0];
    if (!File) return;
    try { const Settings=ParseProjectToml(await File.text()); ApplyProjectSettings(Settings); if(SetupEntry) SetupEntry.BrowserSettings=Settings; }
    catch(Failure) { Notify('Could not load TOML: '+Failure.message); }
    Event.target.value='';
};
Query('#ChooseProjectIcon').onclick = () => Query('#ProjectIconFile').click();
Query('#ProjectIconFile').onchange = Event => {
    const File = Event.target.files?.[0];
    if (!File) return;
    if (!['image/png','image/jpeg','image/webp'].includes(File.type)) { Notify('Choose a PNG, JPEG, or WebP icon.'); Event.target.value=''; return; }
    if (SetupIconUrl) URL.revokeObjectURL(SetupIconUrl);
    SetupIconUrl = URL.createObjectURL(File); ObjectURLs.push(SetupIconUrl);
    if (!Query('#ProjectIconPath').value.trim()) Query('#ProjectIconPath').value='Assets/'+File.name.replace(/[^a-zA-Z0-9._-]/g,'_');
    UpdateProjectIconPreview(); MarkSetupDirty(); Event.target.value='';
};
Query('#RevealSecrets').onchange = Event => document.querySelectorAll('.secret-field input').forEach(Input=>Input.type=Event.target.checked?'text':'password');
Query('#ProjectName').value = 'Default Project';
Query('#ProjectId').value = CreateProjectId();
ConfigureServiceFields();

//------------------------------------------------------------------------------------------------------------------------
//                                                     STABLE PANEL COLLAPSE
//------------------------------------------------------------------------------------------------------------------------

let PaneSettle=0;
function ConfigurePanelCollapse(Side, Collapsed)
{
    if (document.body.classList.contains(Side+'-collapsed')!==Collapsed)
    {
        // The live figure keeps its last frame (stretched) while the columns ease, then renders once at the final size.
        document.body.classList.add('pane-animating');
        clearTimeout(PaneSettle);
        PaneSettle=setTimeout(()=>{document.body.classList.remove('pane-animating');if(!Query('#Reader').hidden)ConfigurePaperScale(Zoom);if(LiveRenderer)LiveRenderer.Refresh();},380);
    }
    document.body.classList.toggle(Side+'-collapsed',Collapsed);
    document.querySelectorAll('[data-collapse="'+Side+'"]').forEach(Button=>{
        Button.setAttribute('aria-expanded',String(!Collapsed));
        Button.setAttribute('aria-label',(Collapsed?'Expand ':'Collapse ')+Side+' panel');
        Button.title=Button.getAttribute('aria-label');
        Button.textContent=Side==='left' ? (Collapsed?'›':'‹') : (Collapsed?'‹':'›');
    });
}
document.querySelectorAll('[data-collapse]').forEach(Button=>Button.onclick=()=>{
    const Side=Button.dataset.collapse;
    const Collapsed=!document.body.classList.contains(Side+'-collapsed');
    if (!Collapsed && innerWidth<700) ConfigurePanelCollapse(Side==='left'?'right':'left',true);
    ConfigurePanelCollapse(Side,Collapsed);
});
ConfigurePanelCollapse('left',innerWidth<600);
ConfigurePanelCollapse('right',innerWidth<1000);

window.addEventListener('resize',()=>{if(innerWidth<600)ConfigurePanelCollapse('left',true);if(innerWidth<1000)ConfigurePanelCollapse('right',true);});


//------------------------------------------------------------------------------------------------------------------------
//                                                     DRAGGABLE PANEL EDGES
//------------------------------------------------------------------------------------------------------------------------

// Each side pane gets a drag handle on its inner edge. Dragging resizes the open width (kept under browser-local
// frontier.browser.paneWidths); dragging a pane narrower than PaneCollapseSnap collapses it, dragging a collapsed rail out
// reopens it, double-click restores the default width and the arrow keys nudge by 16 px.
const PaneLimits={
    left:{Minimum:160,Maximum:380,Default:204,Variable:'--rail-open-width'},
    right:{Minimum:240,Maximum:520,Default:302,Variable:'--inspector-open-width'}
};
const PaneCollapseSnap=112;
const PaneWidthKey='frontier.browser.paneWidths';
const PaneWidths=Object.assign({left:204,right:302},JSON.parse(localStorage.getItem(PaneWidthKey)||'{}'));

function ConfigurePaneWidth(Side, Width)
{
    const Limit=PaneLimits[Side];
    const Other=Side==='left'?'right':'left';
    const OtherWidth=document.body.classList.contains(Other+'-collapsed')?44:PaneWidths[Other];
    const Ceiling=Math.max(Limit.Minimum,Math.min(Limit.Maximum,innerWidth-OtherWidth-320));
    PaneWidths[Side]=Math.round(Math.min(Ceiling,Math.max(Limit.Minimum,Width)));
    document.documentElement.style.setProperty(Limit.Variable,PaneWidths[Side]+'px');
    return PaneWidths[Side];
}

function RetainPaneWidths()
{
    localStorage.setItem(PaneWidthKey,JSON.stringify(PaneWidths));
}

function ConstructPaneResizer(Pane, Side)
{
    const Handle=document.createElement('div');
    Handle.className='pane-resizer';
    Handle.dataset.resize=Side;
    Handle.tabIndex=0;
    Handle.setAttribute('role','separator');
    Handle.setAttribute('aria-orientation','vertical');
    Handle.setAttribute('aria-label','Resize '+Side+' panel');
    Handle.title='Drag to resize · double-click to reset';
    Pane.append(Handle);

    let StartX=0, StartWidth=0, OpenWidth=0;
    Handle.addEventListener('pointerdown',Event=>{
        if (Event.button!==0) return;
        Event.preventDefault();
        Handle.setPointerCapture(Event.pointerId);
        StartX=Event.clientX;
        StartWidth=document.body.classList.contains(Side+'-collapsed')?44:PaneWidths[Side];
        OpenWidth=PaneWidths[Side];
        Handle.classList.add('dragging');
        document.body.classList.add('pane-resizing');
    });
    Handle.addEventListener('pointermove',Event=>{
        if (!Handle.hasPointerCapture(Event.pointerId)) return;
        const Delta=Event.clientX-StartX;
        const Wanted=Side==='left'?StartWidth+Delta:StartWidth-Delta;
        const Collapsed=document.body.classList.contains(Side+'-collapsed');
        if (Wanted<PaneCollapseSnap)
        {
            // Snap shut but remember the width the pane had when the drag began, so reopening restores it.
            if (!Collapsed) ConfigurePanelCollapse(Side,true);
            ConfigurePaneWidth(Side,OpenWidth);
            return;
        }
        ConfigurePaneWidth(Side,Wanted);
        if (Collapsed) ConfigurePanelCollapse(Side,false);
    });
    const Release=Event=>{
        if (!Handle.hasPointerCapture(Event.pointerId)) return;
        Handle.releasePointerCapture(Event.pointerId);
        Handle.classList.remove('dragging');
        document.body.classList.remove('pane-resizing');
        RetainPaneWidths();
        if (LiveRenderer) LiveRenderer.Refresh();
    };
    Handle.addEventListener('pointerup',Release);
    Handle.addEventListener('pointercancel',Release);
    Handle.addEventListener('dblclick',()=>{
        ConfigurePaneWidth(Side,PaneLimits[Side].Default);
        ConfigurePanelCollapse(Side,false);
        RetainPaneWidths();
    });
    Handle.addEventListener('keydown',Event=>{
        if (Event.key!=='ArrowLeft' && Event.key!=='ArrowRight') return;
        Event.preventDefault();
        const Grow=(Event.key==='ArrowRight')===(Side==='left');
        ConfigurePanelCollapse(Side,false);
        ConfigurePaneWidth(Side,PaneWidths[Side]+(Grow?16:-16));
        RetainPaneWidths();
    });
}

document.querySelectorAll('.left-pane').forEach(Pane=>ConstructPaneResizer(Pane,'left'));
document.querySelectorAll('.right-pane').forEach(Pane=>ConstructPaneResizer(Pane,'right'));
if (innerWidth>700) { ConfigurePaneWidth('left',PaneWidths.left); ConfigurePaneWidth('right',PaneWidths.right); }
// The live renderer follows the column while it eases open or closed.

function ConfigurePaperScale(Amount)
{
    // The sheet is laid out at A4 (794 px = 210 mm) and fitted to the desk width; Amount is the zoom on top of the fit.
    const Paper=Query('#PaperPage');
    const Desk=Query('.document-desk');
    if(!Paper||!Desk)return;
    const DeskStyle=getComputedStyle(Desk);
    const Room=Math.max(200,Desk.clientWidth-parseFloat(DeskStyle.paddingLeft)-parseFloat(DeskStyle.paddingRight));
    const Scale=Math.min(1,Room/794)*Amount/100;
    Paper.style.zoom=String(Scale);
    Query('#ZoomReset').textContent=Math.round(Amount)+'%';
}
new ResizeObserver(()=>{if(!Query('#Reader').hidden)ConfigurePaperScale(Zoom);}).observe(Query('.document-desk'));
