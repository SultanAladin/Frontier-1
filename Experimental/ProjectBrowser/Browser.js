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
const Catalogue = [
    {Token:'cornell',Title:'Cornell-Studio Gallery',Extension:'.project',Category:'project',Description:'Tier 1 GTX WebGPU/WebGL2 OpenPBR showcase with 24 studio cameras, lights and track props.',Art:'Valley',Context:'3 levels · 24 props · 1 document',Opened:'Just now',Path:'D:/Frontier/Projects/Cornell-Studio Gallery/Gallery.project',Views:['Studio','Courtyard','Workshop'],Notes:[['Sam','S','Studio cameras, LTC lights and FIA track props ready in Construct & ContentBrowser.','Just now']],Revisions:['WebGPU + 24 studio & track props','OpenPBR 3D thumbnails & UV/Forces','Initial Cornell-Studio gallery']},
    {Token:'valley',Title:'Actuator studio',Extension:'.project',Category:'project',Description:'A small studio scene around the reference actuator assembly.',Art:'Valley',Context:'3 levels · 1 document',Opened:'Today, 10:42',Path:'D:/Frontier/Projects/Actuator studio/Studio.project',Views:['Studio','Courtyard','Workshop'],Notes:[['Sam','S','Review the clearance around the central specimen.','2 hours ago'],['Alex','A','The alternate studio camera is ready for review.','Yesterday']],Revisions:['Copper winding and studio props','Shaft and fasteners','Housing blockout']},
    {Token:'mount',Title:'Precision mount',Extension:'.assembly',Category:'cad',Description:'A compact mounting assembly, built around a machined central sleeve.',Art:'Assembly',Context:'2 assemblies · 2 sketches',Opened:'Today, 09:18',Path:'D:/Frontier/CAD/Precision mount/Mount.assembly',Views:['Main assembly','Sleeve assembly','Sketch 01','Sketch 02'],Notes:[['Sam','S','Check the clearance around the sleeve before the next revision.','45 minutes ago']],Revisions:['Copper winding rings','Shaft and fasteners','Initial housing']},
    {Token:'stone',Title:'Actuator housing',Extension:'.geometry',Category:'geometry',Description:'Actuator housing geometry extracted from the reference specimen.',Art:'Geometry',Context:'Geometry · UV · Texture',Opened:'Yesterday',Path:'D:/Frontier/Geometry/Actuator housing/Housing.geometry',Views:['Geometry','UV layout','Texture','Metadata'],Notes:[['Alex','A','Keep the UV seams on the underside.','Yesterday']],Revisions:['Full housing proportions','Reduced housing study','Initial compact housing']},
    {Token:'island',Title:'Studio exploration',Extension:'.project',Category:'project',Description:'Three camera views of the reference specimen in a compact studio.',Art:'Valley',Context:'3 levels · 1 document',Opened:'Yesterday',Path:'E:/Studies/Island/Island.project',Views:['Studio','Courtyard','Workshop'],Tint:'hue-rotate(24deg) saturate(.6)',Notes:[],Revisions:['Lookout composition','Crossing layout','Initial project']},
    {Token:'bracket',Title:'Actuator sketches',Extension:'.sketch',Category:'cad',Description:'Orthographic contour views of the reference actuator specimen.',Art:'Assembly',Context:'3 sketches · 1 document',Opened:'Oct 14',Path:'E:/Design/Bracket/Bracket.sketch',Views:['Sketch 01','Sketch 02','Sketch 03'],Tint:'saturate(.3)',Notes:[],Revisions:['Mounting hole positions','Outer contour','Initial sketch']},
    {Token:'faceted',Title:'Housing study',Extension:'.geometry',Category:'geometry',Description:'A second housing inspection fixture using the reference surface renderer.',Art:'Geometry',Context:'Geometry · UV · Texture',Opened:'Oct 12',Path:'D:/Studies/Faceted/Surface.geometry',Views:['Geometry','UV layout','Texture','Metadata'],Tint:'hue-rotate(40deg)',Notes:[],Revisions:['Surface colour study','UV unwrap','Initial geometry']}
];
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
const Stars = new Set(RetrievePreference('frontier.prototype.browser.stars', ['valley','mount']));
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
    Query('#CollectionCount').textContent = `${Entries.length} ${Entries.every(Entry => !Entry.Imported) ? 'sample ' : ''}file${Entries.length === 1 ? '' : 's'}`;
    Query('#CollectionTitle').textContent = ({all:'Your workspace',recent:'Recently opened',starred:'Starred files',project:'Projects',cad:'CAD',geometry:'Geometry'})[ActiveFilter];
    Query('#Empty').hidden = Entries.length !== 0;
    Query('#Cards').innerHTML = Entries.map(Entry => `<article class="file-card"><button class="card-preview" data-open="${Escape(Entry.Token)}" aria-label="Preview ${Escape(Entry.Title)}">${ConstructThumbnail(Entry, 0, 0)}${Entry.Imported ? '<span class="preview-caption-tag">LOCAL FILE</span>' : ''}</button><button class="star-button" data-star="${Escape(Entry.Token)}" aria-label="${Stars.has(Entry.Token) ? 'Unstar' : 'Star'} ${Escape(Entry.Title)}" aria-pressed="${Stars.has(Entry.Token)}">${Icon('star')}</button><div class="card-text"><div class="card-title"><button data-open="${Escape(Entry.Token)}">${Escape(Entry.Title)}</button><span class="extension">${Escape(Entry.Extension)}</span></div><div class="card-subtitle">${Escape(Entry.Context)}</div><div class="card-meta"><span class="category-label"><span class="format-dot ${Entry.Category}"></span>${({project:'Project',cad:'CAD',geometry:'Geometry',document:'Document'})[Entry.Category]}</span><time>${Escape(Entry.Opened)}</time></div></div></article>`).join('');
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
    Query('#ReaderThemeSlot').append(Query('#ThemeControl'));
    Query('#ReaderName').textContent = Selected.Title + Selected.Extension;
    Query('#ReaderFormat').textContent = Selected.Extension;
    Query('#RailTitle').textContent = ({project:'Levels',cad:Selected.Extension === '.sketch' ? 'Sketches' : 'Assembly',geometry:'Views',document:'Document'})[Selected.Category];
    Query('#RailCount').textContent = Selected.Views.length;
    Query('#Document').hidden = Selected.Category === 'document';
    Query('#Document small').textContent = Selected.Imported ? 'Browser-generated summary' : 'Sample document';
    ConfigureStarButton();
    ConstructSequence();
    ConstructPreview();
    ConstructInspector();
    Query('#Back').focus();
}
function ConstructSequence()
{
    Query('#PreviewSequence').innerHTML = Selected.Views.map((Title, Index) => `<button class="preview-thumb" data-preview="${Index}" aria-current="${!DocumentEnabled && Index === PreviewIndex}">${ConstructThumbnail(Selected,Index,ActiveRevision)}<span>${Escape(Title)}</span></button>`).join('');
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
    Zoom=100;Query('#ZoomReset').textContent='100%';
    if(DocumentEnabled)ConfigurePaperScale(100);
    ConstructSequence();
    Query('#Previous').disabled=DocumentEnabled||Selected.Views.length<=1;
    Query('#Next').disabled=DocumentEnabled||Selected.Views.length<=1;
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
    const Artwork = Query('#PreviewCanvas>.artwork, #PreviewCanvas .vulkan-capture');
    if (Artwork) Artwork.style.transform = `scale(${Zoom/100})`;
    const Paper=Query('#PreviewCanvas .document-page');
    if (Paper) ConfigurePaperScale(Zoom);
    if (LiveRenderer && Query('#PreviewCanvas').contains(LiveRenderer.Canvas)) { LiveRenderer.Camera.Distance=(Selected.Category==='project' ? 17 : 10.5)*100/Zoom; LiveRenderer.Refresh();CaptureCurrentPreview(); }
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
        Surface.innerHTML = `<div class="detail-hero">${ConstructThumbnail(Selected,PreviewIndex,ActiveRevision)}</div><h3>${Escape(Selected.Title+Selected.Extension)}</h3><p>${Selected.Imported ? 'Selected from your device' : 'Sample file · local workspace'}</p><div class="label">Description</div><p>${Escape(Selected.Description)}</p><div class="crossline"></div><div class="detail-list"><span>Workspace</span><strong>${({project:'Game project',cad:'Drafting / CAD',geometry:'Geometry',document:'Document'})[Selected.Category]}</strong><span>${Selected.Category === 'project' ? 'Levels' : Selected.Category === 'cad' ? 'Assemblies / sketches' : 'Preview views'}</span><strong>${Selected.Views.length}</strong><span>Last opened</span><strong>${Escape(Selected.Opened)}</strong><span>Storage</span><strong>${Selected.Imported ? 'Local file selection' : 'Sample location'}</strong></div><div class="label">Location</div><div class="source-path">${Escape(Selected.Path)}</div><span class="local-chip">${Selected.Imported ? 'Read-only · native decoder pending' : 'Sample file · read-only'}</span>`;
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
Query('#Cards').onclick = Event => { const Star = Event.target.closest('[data-star]'); const Open = Event.target.closest('[data-open]'); if (Star) ToggleStar(Star.dataset.star); else if (Open) ActivateFile(Open.dataset.open); };
Query('#PreviewSequence').onclick = Event => { const Button=Event.target.closest('[data-preview]'); if (Button) { PreviewIndex=Number(Button.dataset.preview);DocumentEnabled=false;ConstructPreview(); } };
Query('#Document').onclick = () => { DocumentEnabled=true;ConstructPreview(); };
Query('#Previous').onclick = () => { PreviewIndex=(PreviewIndex+Selected.Views.length-1)%Selected.Views.length;DocumentEnabled=false;ConstructPreview(); };
Query('#Next').onclick = () => { PreviewIndex=(PreviewIndex+1)%Selected.Views.length;DocumentEnabled=false;ConstructPreview(); };
Query('#ZoomIn').onclick = () => ConfigureZoom(Zoom+25);
Query('#ZoomOut').onclick = () => ConfigureZoom(Zoom-25);
Query('#ZoomReset').onclick = Query('#Fit').onclick = () => ConfigureZoom(100);
Query('#StarReader').onclick = () => ToggleStar(Selected.Token);
document.querySelectorAll('[data-tab]').forEach(Button => Button.onclick = () => { ActiveTab=Button.dataset.tab;ConstructInspector(); });
Query('#InspectorContent').addEventListener('click',Event=>{const Button=Event.target.closest('[data-revision]');if(Button) ConfigureRevision(Number(Button.dataset.revision));});
Query('#RevisionSelect').onchange=Event=>ConfigureRevision(Number(Event.target.value));
Query('#CommentForm').onsubmit = Event => { Event.preventDefault();const Text=Query('#CommentText').value.trim();if (!Text) return; if (!Array.isArray(LocalNotes[Selected.Token])) LocalNotes[Selected.Token]=[];LocalNotes[Selected.Token].push({Author:'You',Initial:'Y',Text:Text.slice(0,2000),Time:'Just now',Sample:false});PersistPreference('frontier.prototype.browser.notes',LocalNotes);Query('#CommentText').value='';ConstructInspector();Notify('Note saved in this browser. It has not been written to a project file.'); };
Query('#OpenEditor').onclick = () => {
    const TargetUrl = `../FrontierWorldMirror/index.html?project=${encodeURIComponent(Selected.Title)}`;
    Query('#LaunchDescription').textContent = Selected.Imported
        ? 'Open this local selection in the Frontier World Editor workspace.'
        : 'Open this project directly in the live Frontier World Editor workspace.';
    Query('#LaunchTarget').textContent = ({project:'Frontier · Project workspace',cad:'Frontier · Drafting workspace',geometry:'Frontier · Geometry workspace',document:'Frontier · Document preview'})[Selected.Category];
    Query('#LaunchFile').textContent = Selected.Path;
    const LaunchLink = Query('#LaunchFrontierNow');
    if (LaunchLink) LaunchLink.href = TargetUrl;
    Query('#LaunchDialog').showModal();
};
Query('#CloseDialog').onclick = Query('#ReturnPreview').onclick = () => Query('#LaunchDialog').close();
document.addEventListener('keydown', Event => { if (Event.target.tagName==='CANVAS' || ['INPUT','TEXTAREA','SELECT'].includes(Event.target.tagName) || Query('#LaunchDialog').open) return;if (Event.key==='/' && !Query('#Library').hidden) { Event.preventDefault();Query('#Search').focus(); } if (Event.key==='Escape' && !Query('#Reader').hidden) ReturnLibrary();if (Selected && Event.key==='ArrowRight') Query('#Next').click();if (Selected && Event.key==='ArrowLeft') Query('#Previous').click(); });
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


//------------------------------------------------------------------------------------------------------------------------
//                                                     STABLE PANEL COLLAPSE
//------------------------------------------------------------------------------------------------------------------------

function ConfigurePanelCollapse(Side, Collapsed)
{
    document.body.classList.toggle(Side+'-collapsed',Collapsed);
    document.querySelectorAll('[data-collapse="'+Side+'"]').forEach(Button=>{
        Button.setAttribute('aria-expanded',String(!Collapsed));
        Button.setAttribute('aria-label',(Collapsed?'Expand ':'Collapse ')+Side+' panel');
        Button.title=Button.getAttribute('aria-label');
        Button.textContent=Side==='left' ? (Collapsed?'›':'‹') : (Collapsed?'‹':'›');
    });
    if (LiveRenderer) requestAnimationFrame(()=>LiveRenderer.Refresh());
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

function ConfigurePaperScale(Amount)
{
    const Paper=Query('#PreviewCanvas .document-page');
    if(!Paper)return;
    const Viewer=Query('.viewer-scroll');
    let Reserved=0;
    for(const Selector of ['.preview-heading','.preview-caption','.viewer-toolbar','.preview-bottom'])
    {
        const Surface=Query(Selector),Style=getComputedStyle(Surface);
        Reserved+=Surface.offsetHeight+parseFloat(Style.marginTop)+parseFloat(Style.marginBottom);
    }
    const ViewerStyle=getComputedStyle(Viewer);
    const AvailableHeight=Math.max(160,Viewer.clientHeight-Reserved-parseFloat(ViewerStyle.paddingTop)-parseFloat(ViewerStyle.paddingBottom)-44);
    const Scale=Math.min(1,Query('#PreviewCanvas').clientWidth/794,AvailableHeight/Paper.offsetHeight)*Amount/100;
    Paper.style.zoom=String(Scale);
    Query('#ZoomReset').textContent=Math.round(Scale*100)+'%';
}
new ResizeObserver(()=>{if(DocumentEnabled)ConfigurePaperScale(Zoom);}).observe(Query('.viewer-scroll'));

