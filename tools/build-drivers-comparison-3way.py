#!/usr/bin/env python3
"""Build the three-way driver comparison page.

Extends the original two-driver page with a third column: the same frame
through our backend as it stands today.  The genesys scan cannot be redone --
its raw data from 2026-10-09 is gone -- so that column, and the October column
beside it, are the JPEGs published at the time, re-used unchanged.

Those were converted by the negative pipeline as it stood in October, which
has itself moved, so the tone difference between the October and the current
column is mostly the converter.  The backend on its own is isolated in the
last section, where two of our own raws taken a day apart go through one and
the same conversion.

  build-drivers-comparison-3way.py <template.html> <asset-dir> <out.html>
"""
import base64
import json
import os
import sys

# key in the page's IMG table -> file in the asset directory
ASSETS = {
    # as published, 2026-10-09
    "genesys": "genesys.jpg", "oct": "ours.jpg",
    "face_g": "face_g.jpg", "tulip_g": "tulip_g.jpg", "flowers_g": "flowers_g.jpg",
    "face_t": "face_o.jpg", "tulip_t": "tulip_o.jpg", "flowers_t": "flowers_o.jpg",
    # the same frame today
    "now": "ours_now.jpg",
    "face_n": "face_n.jpg", "tulip_n": "tulip_n.jpg", "flowers_n": "flowers_n.jpg",
    # backend isolated: our own raws, one converter
    "wall_prev": "wall_prev.jpg", "wall_now": "wall_now.jpg",
}

# Scan windows and the film edge, measured on the shared 889x645 canvas and
# carried over from the original page.  Our window has not moved since.
OVERLAYS = (
    '<div class="ov win n" style="left:2.8121%;top:0.0000%;'
    'width:97.1879%;height:100.0000%"></div>'
    '<div class="ov win g" style="left:0.0000%;top:5.2713%;'
    'width:97.1879%;height:90.8527%"></div>'
    '<div class="ov film" style="left:3.1496%;top:6.0465%;'
    'width:94.6007%;height:88.5271%"></div>'
)

CROPS = (("face", "the face"), ("tulip", "a tulip"), ("flowers", "ground flowers"))


def crop_markup():
    out = []
    for key, title in CROPS:
        out.append(
            f'<div class="crop">'
            f'<p class="cap"><span>{title}</span>'
            f'<button class="swap" type="button">switch pair &#8635;</button></p>'
            f'<div class="stage wipe" data-crop="{key}">'
            f'<div class="layer base"><img class="cb" alt="{title}, right-hand scan"></div>'
            f'<div class="layer top"><img class="ca" alt="{title}, left-hand scan"></div>'
            f'<span class="tag l"></span><span class="tag r"></span>'
            f'<div class="handle"></div>'
            f'</div></div>'
        )
    return "\n    ".join(out)


def main():
    tpl_path, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    img, total = {}, 0
    for key, name in ASSETS.items():
        with open(os.path.join(src, name), "rb") as f:
            raw = f.read()
        total += len(raw)
        img[key] = "data:image/jpeg;base64," + base64.b64encode(raw).decode()

    with open(tpl_path) as f:
        html = f.read()
    html = html.replace("@@OV@@", OVERLAYS)
    html = html.replace("@@CROPS@@", crop_markup())
    html = html.replace("/*IMG*/", json.dumps(img))
    with open(out, "w") as f:
        f.write(html)
    print(f"{out}: {len(ASSETS)} images, {total/1e6:.2f} MB of JPEG, "
          f"{os.path.getsize(out)/1e6:.2f} MB of page")


if __name__ == "__main__":
    main()
