"""Convert the retained lunar albedo source image into the runtime PPM the engine loads.

Source: EngineContent/CelestialMaps/MoonAlbedoSource2048.jpg (2048x1024 equirectangular,
the actual VehicleHMI luna_2k.jpg; see
EngineContent/CelestialMaps/MoonAlbedoProvenance.md). Output: MoonAlbedo2048.ppm (binary P6),
sampled by Frontier::LunarAlbedoMap with U = (selenographic east longitude + 180 deg) / 360 deg
and V = (90 deg - latitude) / 180 deg. Requires Pillow (PyPI).

Usage: PrepareMoonAlbedo.py [source_dir]
"""

import sys
from pathlib import Path

from PIL import Image


def main():
    folder = Path(sys.argv[1] if len(sys.argv) > 1 else "EngineContent/CelestialMaps")
    source = Image.open(folder / "MoonAlbedoSource2048.jpg").convert("RGB")
    target = folder / "MoonAlbedo2048.ppm"
    source.save(target)
    grey = source.convert("L")
    values = list(grey.getdata())
    print(f"wrote {target} size={source.size} mean={sum(values) / len(values):.1f} "
          f"min={min(values)} max={max(values)}")


if __name__ == "__main__":
    main()
