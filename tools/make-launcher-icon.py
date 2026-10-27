#!/usr/bin/env python3
"""Build the Windows launcher's icon files from wfweb's favicon.

Renders resources/web-shared/favicon.svg, the one wfweb mark there is,
into launcher/res/wfweb.png (256x256, window and tray icon) and
launcher/res/wfweb.ico (16..256 px, the exe icon qmake embeds through
RC_ICONS).

Needs resvg-py (SVG rasteriser, self-contained wheel) and Pillow, which the
rest of the project does not. Run it once in a throwaway environment and
commit the outputs; the build never runs it:

    python -m venv /tmp/icoenv && /tmp/icoenv/bin/pip install resvg-py pillow
    /tmp/icoenv/bin/python tools/make-launcher-icon.py
"""

import io
import sys
from pathlib import Path

try:
    import resvg_py
    from PIL import Image
except ImportError as exc:
    sys.exit(f"{exc.name} is required: pip install resvg-py pillow (see the docstring)")

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "resources" / "web-shared" / "favicon.svg"
OUT_DIR = ROOT / "launcher" / "res"
MASTER = 256
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def render(svg: str, size: int) -> Image.Image:
    png = resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size)
    return Image.open(io.BytesIO(bytes(png))).convert("RGBA")


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    svg = SOURCE.read_text(encoding="utf-8")

    master = render(svg, MASTER)
    master.save(OUT_DIR / "wfweb.png", optimize=True)

    # One frame per size, each rendered from the vector, so the small ones
    # are crisp rather than downscaled.
    frames = [render(svg, s) for s in ICO_SIZES]
    frames[-1].save(OUT_DIR / "wfweb.ico", format="ICO",
                    sizes=[(s, s) for s in ICO_SIZES], append_images=frames[:-1])
    print(f"wrote {OUT_DIR / 'wfweb.png'} and {OUT_DIR / 'wfweb.ico'} "
          f"({', '.join(map(str, ICO_SIZES))} px) from {SOURCE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
