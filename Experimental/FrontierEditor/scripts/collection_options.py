"""Register collection candidates in the main native-SVG library."""
from pathlib import Path
import re


def register_collection_options(icon):
    root = Path(__file__).resolve().parents[1]
    for slug, name, description in [
        ('bracketed-objects', 'Collection · Objects in brackets', 'Different objects enclosed by collection boundary corners.'),
        ('stacked-layers', 'Collection · Stacked layers', 'Three layered tiles representing an ordered set.'),
        ('linked-nodes', 'Collection · Linked nodes', 'Connected references in a triangular network.'),
        ('overlapping-tiles', 'Collection · Overlapping tiles', 'A compact fan of item cards; collection design option 04.'),
    ]:
        svg = (root / 'ui-icons' / 'collection-options' / f'{slug}.svg').read_text()
        body = re.sub(r'<title\b[^>]*>.*?</title>', '', svg[svg.index('>') + 1:svg.rindex('</svg>')])
        icon('collection-' + slug, name, 'Organization', description, '',
             '<g transform="scale(5.333333333333333)">' + body + '</g>')
