"""Emit source with explicit fixture adaptations for measurements and regression tests."""
from pathlib import Path
import json
import xml.etree.ElementTree as ET
from slate_refinements import refine_light
ROOT=Path(__file__).resolve().parents[1]
assets=json.loads((ROOT/'vendor/slate-new-icons/icons.json').read_text())
for asset in assets:
    svg=ET.fromstring(asset['svg'])
    refine_light(svg,asset['id'])
    asset['svg']=ET.tostring(svg,encoding='unicode')
print(json.dumps(assets))
