"""Integrate the pinned upstream SVG snapshot without restyling its artwork."""
from pathlib import Path
import json
import re
import xml.etree.ElementTree as ET
from slate_refinements import refine_light

SOURCE='https://github.com/SultanAladin/Slate/tree/aaa87a26945c64d712005707a56ecfecf7a6275f/References/NewIcons'


def load_slate_icons(root):
    assets=json.loads((root/'vendor/slate-new-icons/icons.json').read_text())
    selected=json.loads((root/'vendor/slate-new-icons/selection.json').read_text())
    allowed=set(selected)
    if len(allowed)!=len(selected) or not allowed.issubset({a['id'] for a in assets}):
        raise ValueError('Invalid Slate selection')
    # Prune rejected imports, including stale production exports on regeneration.
    for directory in [root/'custom-icons',root/'dist/custom-icons']:
        for old in directory.glob('slate-*.svg'):
            if old.stem not in allowed:old.unlink()
    assets=[a for a in assets if a['id'] in allowed]
    layouts=json.loads((root/'vendor/slate-new-icons/layout.json').read_text())
    entries=[]
    for asset in assets:
        key=asset['id']
        svg=ET.fromstring(asset['svg'])
        if svg.tag!='svg':raise ValueError(key)
        refine_light(svg,key)
        layout=layouts[key]
        scene=next(e for e in svg if e.tag=='g')
        badge=list(scene)[-1]
        if badge.tag!='g' or 'filter' not in badge.attrib:raise ValueError(f'Unexpected badge: {key}')
        main=ET.Element('g',{'data-slate-layout':'main','transform':'matrix('+ ' '.join(map(str,layout['mainTransform'])) +')'})
        corner=ET.Element('g',{'data-slate-layout':'badge','data-corner':layout['side'],'transform':'matrix('+ ' '.join(map(str,layout['badgeTransform'])) +')'})
        for child in list(scene):
            scene.remove(child)
            (corner if child is badge else main).append(child)
        scene.extend([main,corner])
        ids=[e.get('id') for e in svg.iter() if e.get('id')]
        if len(ids)!=len(set(ids)):raise ValueError(f'Duplicate upstream IDs in {key}')
        mapping={i:key+'--'+i for i in ids}
        for e in svg.iter():
            if e.tag in ['script','image','foreignObject']:raise ValueError(f'Non-native content: {key}')
            for attr,value in list(e.attrib.items()):
                if attr.lower().startswith('on'):raise ValueError(f'Event handler in {key}')
                if attr in ['href','{http://www.w3.org/1999/xlink}href']:
                    if not value.startswith('#'):raise ValueError(f'External resource in {key}')
                    value='#'+mapping[value[1:]]
                if attr=='id':value=mapping[value]
                def local(m):
                    if m[1] not in mapping:raise ValueError(f'Unresolved reference in {key}: {m[1]}')
                    return f'url(#{mapping[m[1]]})'
                value=re.sub(r'url\(#([^\)]+)\)',local,value)
                e.set(attr,value)
        svg.set('xmlns','http://www.w3.org/2000/svg')
        svg.set('role','img')
        svg.set('aria-labelledby',key+'-title')
        title=ET.Element('title',id=key+'-title');title.text=asset['name']+' — Slate / NewIcons';svg.insert(0,title)
        # Retain local shadows, not the old page-level shadow on enlarged exports.
        svg.attrib.pop('class',None)
        raw=ET.tostring(svg,encoding='unicode')
        (root/'custom-icons'/f'{key}.svg').write_text(raw)
        entries.append(dict(id=key,name=asset['name'],group='Slate',description='Adapted from Slate / NewIcons: enlarged main artwork and a smaller bottom-corner badge. Spotlight, LED Panel and Lantern include user-requested fixture corrections.',svg=raw,source=SOURCE,viewBox=svg.get('viewBox')))
    return entries
