"""Reference-inspired charcoal asset folders and format-specific file sheets."""
from html import escape

TYPES={
 'materials':('Materials','#b194d0','material'),
 'images':('Images','#81b89d','image'),
 'audio':('Audio','#7fa6d0','audio'),
 'video':('Video','#cc8b91','video'),
 'models':('3D models','#77b4bf','model'),
 'scenes':('Scenes','#a2b87c','scene'),
 'scripts':('Scripts','#c4b773','script'),
 'shaders':('Shaders','#a092d4','shader'),
 'textures':('Textures','#bca080','texture'),
 'animation':('Animation','#d49c77','animation'),
 'fonts':('Fonts','#b993b8','font'),
 'archives':('Archives','#baa269','archive'),
}
# Illustrative metadata for standalone SVG previews, not live filesystem totals.
FOLDER_METADATA={
 'materials':('18 MB',24), 'images':('128 MB',48), 'audio':('256 MB',16),
 'video':('1.2 GB',8), 'models':('384 MB',12), 'scenes':('64 MB',6),
 'scripts':('240 KB',32), 'shaders':('96 KB',18), 'textures':('512 MB',36),
 'animation':('48 MB',20), 'fonts':('12 MB',14), 'archives':('2.4 GB',5),
}
FORMATS=[('PNG','images'),('JPG','images'),('SVG','images'),('EXR','textures'),('WAV','audio'),('MP3','audio'),('OGG','audio'),('MAT','materials'),('FBX','models'),('OBJ','models'),('GLTF','models'),('BLEND','scenes'),('JSON','scripts'),('PY','scripts'),('GLSL','shaders'),('ZIP','archives'),('TTF','fonts'),('OTF','fonts'),('WOFF','fonts'),('WOFF2','fonts')]

# Per-format accents are deliberately independent of the parent folder palette.
# Neutral sheets stay unchanged; only badges, recessed motifs and rules are tinted.
FORMAT_COLORS={
 'PNG':'#77bd98', 'JPG':'#6caeb3', 'SVG':'#d3a966', 'EXR':'#aa9acb',
 'WAV':'#78a7cf', 'MP3':'#cc8eaa', 'OGG':'#9cae72', 'MAT':'#b38bd2',
 'FBX':'#7b9fbe', 'OBJ':'#b6ac8c', 'GLTF':'#6bb29a', 'BLEND':'#d39569',
 'JSON':'#c8ba71', 'PY':'#7398c9', 'GLSL':'#9293d0', 'ZIP':'#c39758',
 'TTF':'#c18d9f', 'OTF':'#a992c0', 'WOFF':'#76b4c4', 'WOFF2':'#8ab488',
}
assert len(set(FORMAT_COLORS.values()) | {v[1] for v in TYPES.values()}) == 32

def symbol(kind):
    shapes={
      'image':'<rect x="3" y="5" width="26" height="22" rx="3"/><circle cx="22" cy="11" r="2" fill="currentColor" stroke="none"/><path d="m4 23 8-9 7 8 4-4 5 5"/>',
      'audio':'<path d="M4 14v4m5-8v12m5-17v22m5-19v16m5-12v8m5-5v2"/>',
      'video':'<rect x="3" y="5" width="26" height="22" rx="3"/><path d="m13 11 9 5-9 5Z" fill="currentColor" stroke="none"/><path d="M7 6v20m18-20v20" opacity=".4"/>',
      'model':'<path d="m16 3 12 7v13l-12 7-12-7V10Zm0 14L4 10m12 7 12-7M16 17v13m-6-23 12 7"/>',
      'scene':'<rect x="11" y="3" width="10" height="7" rx="1.5"/><path d="M16 10v7M6 22v-5h20v5"/><rect x="2" y="22" width="9" height="7" rx="1.5"/><rect x="21" y="22" width="9" height="7" rx="1.5"/>',
      'script':'<path d="m10 8-7 8 7 8m12-16 7 8-7 8M19 5l-6 22"/>',
      'shader':'<rect x="2" y="12" width="9" height="9" rx="2"/><rect x="22" y="2" width="8" height="8" rx="2"/><rect x="22" y="23" width="8" height="8" rx="2"/><path d="M11 16c7 0 4-10 11-10m-11 10c7 0 4 11 11 11"/>',
      'texture':'<rect x="3" y="3" width="26" height="26" rx="3"/><path d="M3 12h26M3 21h26M12 3v26M21 3v26"/><path d="M4 4h8v8H4Zm8 8h9v9h-9Zm9 9h7v7h-7Z" fill="currentColor" stroke="none" opacity=".6"/>',
      'animation':'<path d="M3 26V7m0 19h27M5 23c13 0 8-18 22-18"/><path d="m25 2 4 3-4 4-4-4Z" fill="currentColor" stroke="none"/>',
      'font':'<path d="M3 27 12 5l9 22M7 19h10M21 14h8m-4 0v13"/>',
      'archive':'<rect x="5" y="3" width="22" height="27" rx="3"/><path d="M13 4h5m-5 4h5m-5 4h5m-5 4h5"/><rect x="13" y="20" width="6" height="6" rx="1"/>',
    }
    if kind=='material':return '<circle cx="16" cy="16" r="13" fill="url(#@-orb)" stroke="#d1c6dc" stroke-opacity=".4"/><path d="M7 8c-2 10 9 19 20 14" stroke="#24212a" stroke-width="1.6"/><path d="M8 7c-1 9 9 17 19 14" stroke="#ece4f5" stroke-opacity=".5" stroke-width=".65"/>'
    return shapes[kind]

def glyph(kind,x,y,size,color,opacity=1):
    return f'<g transform="translate({x} {y}) scale({size/32})" color="{color}" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" opacity="{opacity}">{symbol(kind)}</g>'

def defs(tint):return f'''
<radialGradient id="@-orb" cx="30%" cy="25%" r="80%"><stop stop-color="#eee9f2"/><stop offset=".25" stop-color="{tint}"/><stop offset=".65" stop-color="#5e566b"/><stop offset="1" stop-color="#29272e"/></radialGradient>
<filter id="@-shadow" x="-30%" y="-30%" width="160%" height="170%"><feDropShadow dx="0" dy="4" stdDeviation="4" flood-color="#000" flood-opacity=".18"/></filter>'''

def badge(kind,tint,x,y):
    return f'<rect x="{x}" y="{y}" width="43" height="43" rx="11" fill="#393939" stroke="#555555"/><rect x="{x+1}" y="{y+1}" width="41" height="41" rx="10" fill="{tint}" opacity=".12"/>'+glyph(kind,x+8,y+8,27,tint)

def register_file_asset_icons(icon):
    for key,(name,tint,kind) in TYPES.items():
        body='''<g filter="url(#@-shadow)"><rect x="29" y="55" width="195" height="146" rx="20" fill="#303030" stroke="#555555"/>
<g transform="rotate(-5 87 88)"><rect x="45" y="61" width="99" height="112" rx="10" fill="#dededb" stroke="#f3f3ef" stroke-opacity=".4"/>'''
        body+=glyph(kind,60,70,25,'#787a78',.45)+'</g>'
        body+='''<g transform="rotate(9 175 107)"><rect x="130" y="70" width="76" height="104" rx="9" fill="#dededb" stroke="#f6f6f2" stroke-opacity=".5"/>'''+glyph(kind,148,79,23,'#797b7a',.4)+'''</g>
<path d="M29 98q0-23 23-23h55q12 0 23 8l16 10q8 5 18 5h41q24 0 24 24v72q0 23-23 23H52q-23 0-23-23Z" fill="#3c3c3c" stroke="#555555"/>
<path d="M51 78h56q11 0 22 8l16 10q9 5 20 5h39" stroke="#fff" stroke-opacity=".09"/>
<path d="M48 212h157" stroke="#000" stroke-opacity=".13"/>
'''+badge(kind,tint,44,159)
        # The name is part of the SVG itself, printed low on the folder pocket.
        size,items=FOLDER_METADATA[key]
        body+=f'<text x="99" y="178" font-family="Arial, sans-serif" font-size="14" font-weight="500" fill="#dededb">{escape(name)}</text><text x="99" y="194" font-family="Arial, sans-serif" font-size="9" font-weight="400" fill="#969694">{escape(size)} · {items} items</text></g>'
        identity='asset-folder-'+key
        icon(identity,name+' folder','Files & folders',f'A charcoal {name.lower()} folder with pale inset content sheets, a solid charcoal pocket with its name printed at the bottom and a small muted semantic badge. Sample size and item count appear below the name (illustrative, not live folder data). Transparent native SVG.',defs(tint).replace('@',identity),body.replace('@',identity))
    for fmt,category in FORMATS:
        name,_,kind=TYPES[category]
        tint=FORMAT_COLORS[fmt]
        body='''<g filter="url(#@-shadow)"><g transform="rotate(-16 121 135)"><rect x="56" y="56" width="125" height="154" rx="10" fill="#272727" stroke="#555" stroke-opacity=".4"/><path d="M69 72h51m-51 8h32" stroke="#777" stroke-opacity=".2" stroke-width="2"/></g><g transform="rotate(11 131 139)"><rect x="80" y="47" width="118" height="160" rx="10" fill="#2b2b2b" stroke="#555" stroke-opacity=".4"/></g>
<path d="M76 35h77l40 40v133q0 12-12 12H76q-12 0-12-12V47q0-12 12-12Z" fill="#2d2d2d" stroke="#555555"/>
<path d="M153 35v28q0 12 12 12h28" fill="#303030" stroke="#555555"/>
<path d="M78 49h43m-43 8h26" stroke="#b1b1b1" stroke-opacity=".17" stroke-width="2" stroke-linecap="round"/>
'''+glyph(kind,103,94,66,tint,.28)+badge(kind,tint,53,158)
        body+=f'<text x="110" y="183" font-family="Arial, sans-serif" font-size="{14 if len(fmt)>4 else 17}" font-weight="600" letter-spacing="1" fill="#d0d0cd">{escape(fmt)}</text><path d="M111 192h42" stroke="{tint}" stroke-opacity=".35" stroke-width="2" stroke-linecap="round"/></g>'
        identity='asset-file-'+fmt.lower()
        icon(identity,fmt+' file','Files & folders',f'{fmt} format: layered solid-charcoal file sheets, a folded corner, recessed {name.lower()} motif and subtly tinted badge. No raster artwork.',defs(tint).replace('@',identity),body.replace('@',identity))
