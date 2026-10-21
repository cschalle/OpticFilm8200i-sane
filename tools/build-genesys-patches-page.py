#!/usr/bin/env python3
"""Build docs/comparison/genesys-patches.html: upstream genesys before and after the patches.

The page is one self-contained file (images embedded), in the style of
docs/comparison/drivers-comparison.html, whose stylesheet it reuses.

Inputs, all in WORKDIR (default ./page):
  before_600.tif before_3600.tif   upstream genesys, SANE master 982deaef
  after_600.tif  after_3600.tif    the same with docs/upstream-genesys-patches applied
  *_r.ppm                          the above after `nv-cli render` (same pipeline for all)
  pair600_f1_r.ppm pair600_f2.raw  a two-frame scan (--infra-red): rendered image, IR plane
  pair3600_f1_r.ppm pair3600_f2.raw
  metrics.json                     film edge margins and colour, from metrics.py
  after_3600.log                   SANE_DEBUG_GENESYS=5 log of the patched 3600 dpi scan
Run:  tools/build-genesys-patches-page.py [WORKDIR]
"""
import base64, io, json, os, re, sys
import numpy as np
from PIL import Image

Image.MAX_IMAGE_PIXELS = None
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
W = sys.argv[1] if len(sys.argv) > 1 else "page"
OUT = os.path.join(ROOT, "docs", "comparison", "genesys-patches.html")
PATCHES = os.path.join(ROOT, "docs", "upstream-genesys-patches")
GH = "https://github.com/cschalle/OpticFilm8200i-sane/blob/main/docs/upstream-genesys-patches/"
MM = 25.4

M = json.load(open(os.path.join(W, "metrics.json")))


def jpg(im, q=80):
    b = io.BytesIO()
    im.convert("RGB").save(b, "JPEG", quality=q, optimize=True)
    return "data:image/jpeg;base64," + base64.b64encode(b.getvalue()).decode()


def load(name):
    return Image.open(os.path.join(W, name))


# ---- whole frame, 600 dpi, on one film coordinate system (mm) ----------------------------
bm, am = M["before_600"], M["after_600"]
win_w = bm["w"] * MM / 600                      # window width, mm (same for both)
film_w = win_w - bm["left"] - bm["right"]
b_win = (-bm["left"], -bm["top"], win_w, bm["h"] * MM / 600)          # x, y, w, h  (film top-left = 0,0)
a_win = (-am["left"], -am["top"], win_w, am["h"] * MM / 600)
film_h = am["h"] * MM / 600 - am["top"] - am["bottom"]
x0 = min(b_win[0], a_win[0]); y0 = min(b_win[1], a_win[1])
x1 = max(b_win[0] + b_win[2], a_win[0] + a_win[2]); y1 = max(b_win[1] + b_win[3], a_win[1] + a_win[3])
CWm, CHm = x1 - x0, y1 - y0


def pct(win):
    return dict(left=(win[0] - x0) / CWm * 100, top=(win[1] - y0) / CHm * 100,
                width=win[2] / CWm * 100, height=win[3] / CHm * 100)


pb, pa = pct(b_win), pct(a_win)
pf = dict(left=(0 - x0) / CWm * 100, top=(0 - y0) / CHm * 100, width=film_w / CWm * 100, height=film_h / CHm * 100)
css = lambda d: "left:%.4f%%;top:%.4f%%;width:%.4f%%;height:%.4f%%" % (d["left"], d["top"], d["width"], d["height"])
overlays = ('<div class="ov win o" style="%s"></div><div class="ov win g" style="%s"></div>'
            '<div class="ov film" style="%s"></div>') % (css(pa), css(pb), css(pf))
ASPECT = "%.0f/%.0f" % (CWm * 100, CHm * 100)

IMG = {
    "upstream": jpg(load("before_600_r.ppm"), 84),
    "patched": jpg(load("after_600_r.ppm"), 84),
    "colour600": jpg(load("pair600_f1_r.ppm"), 84),
}
ir600 = np.fromfile(os.path.join(W, "pair600_f2.raw"), dtype="<u2").reshape(640, 864).astype(np.float32)
sc = np.percentile(ir600[40:-30, 15:-15], 99.5)
IMG["ir600"] = jpg(Image.fromarray(np.clip(ir600 / sc * 255, 0, 255).astype(np.uint8)), 86)

# ---- 3600 dpi detail crops, before/after, aligned on the film's own corner --------------------
CW_, CH_ = 1000, 750
def origin(k):
    return M[k]["left"] * 3600 / MM, M[k]["top"] * 3600 / MM
ob, oa = origin("before_3600"), origin("after_3600")
bi, ai = load("before_3600_r.ppm"), load("after_3600_r.ppm")
CROPS = {"face": (1914, 1530, "The girl's face and a tulip"),
         "tulip": (2274, 2130, "The large, soft-focus tulip"),
         "flowers": (4000, 3000, "Flowers and soil at the bottom")}
for k, (cx, cy, _) in CROPS.items():
    for tag, im, o in (("u", bi, ob), ("p", ai, oa)):
        x = int(round(o[0] + cx - CW_ / 2)); y = int(round(o[1] + cy - CH_ / 2))
        IMG["%s_%s" % (k, tag)] = jpg(im.crop((x, y, x + CW_, y + CH_)), 80)

# ---- infrared: the colour frame and the IR frame of one two-frame scan ------------------------
ir36 = np.fromfile(os.path.join(W, "pair3600_f2.raw"), dtype="<u2").reshape(3845, 5184).astype(np.float32)
sc36 = np.percentile(ir36[330:-250, 120:-120], 99.5)
col36 = load("pair3600_f1_r.ppm")
IR_CROPS = {"scratch": (4564, 1055, "A scratch and torn emulsion on the right-hand side"),
            "dust": (3420, 1910, "Dust specks and a hair between the stems")}
for k, (cx, cy, _) in IR_CROPS.items():
    x, y = cx - CW_ // 2, cy - CH_ // 2
    IMG["%s_c" % k] = jpg(col36.crop((x, y, x + CW_, y + CH_)), 80)
    c = np.clip(ir36[y:y + CH_, x:x + CW_] / sc36 * 255, 0, 255).astype(np.uint8)
    IMG["%s_i" % k] = jpg(Image.fromarray(c), 82)

# ---- the white-strip statistics from the log ----------------------------------------------------
log = open(os.path.join(W, "after_3600.log"), errors="replace").read()
white_after = re.findall(r"shading white ch(\d) min=(\d+) mean=(\d+) max=(\d+) at_adc_ceiling=(\d+)/(\d+)", log)
gain_codes = re.search(r"gain codes \((\d+),(\d+),(\d+)\)", log).groups()
lit = re.search(r"lit gate at gain 0: \((\d+),(\d+),(\d+)\)", log).groups()

# ---- patch list, with diffstat computed from the .patch files --------------------------------
def patch_info(fn):
    t = open(os.path.join(PATCHES, fn)).read()
    subj = re.search(r"^Subject: \[PATCH[^\]]*\] (.*?)\n(?=\S)", t, re.S | re.M).group(1).replace("\n ", " ")
    files = {}
    cur = None
    for line in t.splitlines():
        m = re.match(r"diff --git a/(\S+)", line)
        if m:
            cur = m.group(1); files[cur] = [0, 0]; continue
        if cur and line.startswith("+") and not line.startswith("+++"): files[cur][0] += 1
        if cur and line.startswith("-") and not line.startswith("---"): files[cur][1] += 1
    return subj, files

SUMMARY = {
    1: "The default window clipped the film at the right-hand edge and left 0.1&ndash;0.4&nbsp;mm above and below the frame. The window now covers the whole gate with the film centred.",
    2: "SilverFast's fixed front-end gain drove the white strip to the ADC ceiling on this unit, so the shading table was built from clipped data. The gain is now chosen per channel from the lit gate.",
    3: "<code>--depth 8</code> for Color and Gray. The ASIC still scans at 16 bits; the high byte is kept at the end of the pipeline.",
    4: "The <code>Transparency Adapter Infrared</code> source, as the other genesys film scanners offer it: a standalone gray image of the infrared plane.",
    5: "IntelliScan and QuickScan as the sensor options <code>extra</code> and <code>scan</code>, read from the interrupt endpoint. A physical press has not been verified through genesys yet.",
    6: "The <code>genesys.desc</code> entry and a newsfragment.",
    7: "<code>--infra-red</code>: the standard option name. The image frame, then a gray frame of the same area from a second <code>sane_start</code>, with no cancel between.",
}
patch_rows = []
for fn in sorted(f for f in os.listdir(PATCHES) if f.endswith(".patch")):
    n = int(fn[:4]); subj, files = patch_info(fn)
    adds = sum(v[0] for v in files.values()); dels = sum(v[1] for v in files.values())
    names = ", ".join("<code>%s</code>" % f.split("/")[-1] for f in files)
    patch_rows.append(
        '<tr><td>%d</td><td><b>%s</b><span class="sub">%s</span><span class="sub">%s</span></td>'
        '<td class="num">+%d &minus;%d</td><td><a href="%s%s">patch</a></td></tr>'
        % (n, subj.replace("genesys: ", "").replace("gl128: ", ""), SUMMARY.get(n, ""), names, adds, dels, GH, fn))

orig = open(os.path.join(ROOT, "docs", "comparison", "drivers-comparison.html")).read()
STYLE = re.search(r"<style>(.*?)</style>", orig, re.S).group(1)

def f(v, d=2):
    return ("%." + str(d) + "f") % v

def margin_row(label, key):  # one table row
    m = M[key]
    cut = lambda v: ('<td class="neg">%s</td>' % f(v)) if v < 0.1 else "<td>%s</td>" % f(v)
    return "<tr><td>%s</td>%s%s%s%s</tr>" % (label, cut(m["left"]), cut(m["right"]), cut(m["top"]), cut(m["bottom"]))

def pill(kind, text):
    return '<span class="pill %s">%s</span>' % (kind, text)

PU, PP = pill("g", "upstream"), pill("o", "patched")
w_rows = ""
for ch, mn, mean, mx, ceil, n in white_after:
    w_rows += "<tr><td>%s</td><td>%s&ndash;%s</td><td>%d / %s</td></tr>" % ("RGB"[int(ch)], f"{int(mn):,}", f"{int(mx):,}", int(ceil), n)

crop_html = ""
for k, (_, _, cap) in CROPS.items():
    crop_html += ('<div class="crop"><div class="stage wipe" data-set="%s" data-a="%s_u" data-b="%s_p">'
                  '<div class="layer base"><img alt="upstream, %s"></div><div class="layer top"><img alt="patched, %s"></div>'
                  '<span class="tag o">patched</span><span class="tag g">upstream</span>'
                  '<div class="handle" tabindex="0" role="slider" aria-label="Wipe position, %s" aria-valuemin="0" aria-valuemax="100" aria-valuenow="50"></div></div>'
                  '<p class="hint">%s</p></div>') % (k, k, k, k, k, k, cap)
ir_html = ""
for k, (_, _, cap) in IR_CROPS.items():
    ir_html += ('<div class="crop"><div class="stage wipe" data-a="%s_c" data-b="%s_i">'
                '<div class="layer base"><img alt="image frame, %s"></div><div class="layer top"><img alt="infrared frame, %s"></div>'
                '<span class="tag o">infrared</span><span class="tag g">image</span>'
                '<div class="handle" tabindex="0" role="slider" aria-label="Wipe position, %s" aria-valuemin="0" aria-valuemax="100" aria-valuenow="50"></div></div>'
                '<p class="hint">%s</p></div>') % (k, k, k, k, k, cap)

after, before, ours3600 = M["after_3600"], M["before_3600"], M["ours_3600"]

HTML = """<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Upstream genesys, before and after the patches</title>
<style>%(STYLE)s
.num{white-space:nowrap;font-variant-numeric:tabular-nums;color:var(--muted)}
td .sub{display:block;color:var(--muted);font-size:.86em;font-weight:400;margin-top:2px}
td:nth-child(2){text-align:left}
pre{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px 16px;overflow:auto;font-size:.85em;line-height:1.5}
.caveats li{margin:.3em 0}
#irStage img{inset:0;width:100%%;height:100%%}
th:nth-child(2),th:nth-child(1){text-align:left}
</style></head>
<body>
<div class="wrap">
  <h1>Upstream genesys, before and after</h1>
  <p class="lead">The same colour negative on the same OpticFilm 8200i (<code>07b3:1825</code>), scanned once with
  %(PU)s SANE (<code>genesys</code>, master <code>982deaef</code>) and once with the same driver with
  %(PP)s <a href="https://github.com/cschalle/OpticFilm8200i-sane/tree/main/docs/upstream-genesys-patches">seven patches</a>
  applied. Both go through the identical negative conversion, so what you see differs only because the raw data differs.
  The patches bring genesys closer to the <a href="drivers-comparison.html">plustek8200i backend</a> compared on the earlier page.</p>

  <nav class="links" aria-label="Project links">
    <a class="btn primary" href="https://github.com/cschalle/OpticFilm8200i-sane/tree/main/docs/upstream-genesys-patches">The patches <span class="where">on GitHub</span></a>
    <a class="btn" href="https://gitlab.com/sane-project/backends/-/work_items/886">Upstream SANE ticket #886 <span class="where">on GitLab</span></a>
    <a class="btn" href="drivers-comparison.html">Earlier comparison <span class="where">plustek8200i vs genesys</span></a>
  </nav>

  <div class="cards">
    <div class="card"><h3>Speed (full frame)</h3>
      <div class="row"><span>600 dpi</span><span><b>15.4 s</b> &rarr; <b class="o">16.9 s</b></span></div>
      <div class="row"><span>3600 dpi</span><span><b>49.2 s</b> &rarr; <b class="o">53.6 s</b></span></div>
      <div class="row"><span>3600 dpi, with infrared</span><span><b class="o">106 s</b></span></div>
      <div class="row"><span style="color:var(--muted);font-size:.85em">upstream &rarr; patched; the patched scan covers 9&nbsp;%% more film</span><span></span></div></div>
    <div class="card"><h3>Output size at 3600 dpi</h3>
      <div class="row"><span>upstream</span><b>%(bw)d &times; %(bh)d</b></div>
      <div class="row"><span>patched</span><b class="o">%(aw)d &times; %(ah)d</b></div>
      <div class="row"><span>plustek8200i</span><b>%(ow)d &times; %(oh)d</b></div></div>
    <div class="card"><h3>Features</h3>
      <div class="row"><span>Infrared channel</span><span>&#10007; &rarr; <b class="o">&#10003;</b></span></div>
      <div class="row"><span>8-bit output</span><span>&#10007; &rarr; <b class="o">&#10003;</b></span></div>
      <div class="row"><span>IntelliScan / QuickScan</span><span>&#10007; &rarr; <b class="o">&#10003;</b>*</span></div>
      <div class="row"><span style="color:var(--muted);font-size:.85em">* a physical press is not yet verified</span><span></span></div></div>
  </div>

  <h2>1 &middot; Whole frame, 600 dpi</h2>
  <p class="sub">Both scans are drawn on one shared film coordinate system, so the same piece of film lies in the same place in every view. <b>Dark</b> areas are where a scan window did not reach. <b>White</b> areas are inside a window but beyond the film (unexposed film inverts to white).</p>

  <div class="toolbar">
    <div class="seg" role="group" aria-label="View mode" id="modes">
      <button aria-pressed="true" data-mode="wipe">Wipe</button>
      <button aria-pressed="false" data-mode="side">Side by side</button>
      <button aria-pressed="false" data-mode="blink">Blink</button>
    </div>
    <label class="chk"><input type="checkbox" id="chkWin"> <i style="border-color:var(--ours)"></i><i style="border-color:var(--gen)"></i> scan windows</label>
    <label class="chk"><input type="checkbox" id="chkFilm"> <i style="border-color:var(--film);border-top-style:dashed"></i> film edges</label>
    <span class="seg" id="zoomSeg" role="group" aria-label="Zoom" hidden>
      <button aria-pressed="true" data-z="1">Fit</button><button aria-pressed="false" data-z="2">200%%</button><button aria-pressed="false" data-z="3">300%%</button>
    </span>
    <label class="chk" id="autoBlinkLbl" hidden><input type="checkbox" id="autoBlink"> auto-blink</label>
  </div>

  <div class="frame" id="frame">
    <div id="m-wipe">
      <div class="stage" id="wipeStage">
        <div class="layer base"><img class="uimg" alt="Upstream scan"></div>
        <div class="layer top"><img class="pimg" alt="Patched scan"></div>
        %(overlays)s
        <span class="tag o">patched</span><span class="tag g">upstream</span>
        <div class="handle" tabindex="0" role="slider" aria-label="Wipe position" aria-valuemin="0" aria-valuemax="100" aria-valuenow="50"></div>
      </div>
      <p class="hint">Drag anywhere on the picture, or focus the handle and use the arrow keys.</p>
    </div>
    <div id="m-side" hidden>
      <div class="pair">
        <div><p class="cap g">upstream</p><div class="vp" id="vpU"><div class="stage" id="stU"><img class="uimg" alt="Upstream scan">%(overlays)s</div></div></div>
        <div><p class="cap o">patched</p><div class="vp" id="vpP"><div class="stage" id="stP"><img class="pimg" alt="Patched scan">%(overlays)s</div></div></div>
      </div>
      <p class="hint">Zoom with the buttons above, then scroll: both panels move together.</p>
    </div>
    <div id="m-blink" hidden>
      <div class="stage" id="blinkStage" tabindex="0" aria-label="Click or press space to switch between the scans">
        <div class="layer base"><img class="uimg" alt="Upstream scan"></div>
        <div class="layer top" id="blinkTop"><img class="pimg" alt="Patched scan"></div>
        %(overlays)s
        <span class="tag mid" id="blinkTag">patched</span>
      </div>
      <p class="hint">Click the picture or press <kbd>Space</kbd> to flip between the two. Whatever moves is a difference.</p>
    </div>
  </div>

  <h2>2 &middot; Scan area</h2>
  <p class="sub">Distance in millimetres between the film's edge and the edge of the scan window, measured the same way on both scans (the half-brightness point between the holder and the film). A value near zero means the film touches the window edge and a small positioning error would clip the picture.</p>
  <div class="card" style="padding:6px 8px">
    <table>
      <thead><tr><th>600 dpi</th><th>left</th><th>right</th><th>top</th><th>bottom</th></tr></thead>
      <tbody>%(m600u)s%(m600p)s</tbody>
      <thead><tr><th>3600 dpi</th><th></th><th></th><th></th><th></th></tr></thead>
      <tbody>%(m3600u)s%(m3600p)s</tbody>
    </table>
  </div>
  <div class="note"><b>What changed.</b> Upstream's window sat 0.34&nbsp;mm too far toward one edge, so the film touched the window's right-hand edge and was cut off there, and it left 0.1&ndash;0.4&nbsp;mm above and below the frame. The patched window is centred (0.5&nbsp;mm either side) and leaves 1.1&ndash;1.8&nbsp;mm above and below. The film measures the same size in both scans (%(fw).2f &times; %(fh).2f&nbsp;mm), so the holder was not disturbed between them.</div>

  <h2>3 &middot; Colour and calibration</h2>
  <p class="sub">Upstream programs the gain codes SilverFast used in its own captures. On this unit they saturate the empty-gate reading that the shading table is built from. The patched driver reads the lit gate at gain 0 and chooses each channel's gain so the brightest column reaches 87&nbsp;%% of full scale.</p>
  <div class="card" style="padding:6px 8px">
    <table>
      <thead><tr><th>White strip, 3600 dpi</th><th>upstream (columns at the ADC ceiling)</th><th>patched (columns at the ADC ceiling)</th></tr></thead>
      <tbody>
        <tr><td>Red</td><td>62,981&ndash;63,077 &nbsp;(5181 / 5184)</td><td>%(wR)s</td></tr>
        <tr><td>Green</td><td>62,980&ndash;62,981 &nbsp;(5184 / 5184)</td><td>%(wG)s</td></tr>
        <tr><td>Blue</td><td>62,579&ndash;62,579 &nbsp;(5184 / 5184)</td><td>%(wB)s</td></tr>
        <tr><td>Gain codes (R, G, B)</td><td>39, 32, 39 (fixed)</td><td>%(codes)s (from the lit gate: %(lit)s)</td></tr>
      </tbody>
      <thead><tr><th>Rendered, centre of frame</th><th>upstream</th><th>patched &nbsp;(plustek8200i)</th></tr></thead>
      <tbody>
        <tr><td>Median R / G / B</td><td>%(bmed)s</td><td>%(amed)s &nbsp;(%(omed)s)</td></tr>
        <tr><td>G / R, B / R</td><td>%(bgr)s, %(bbr)s</td><td>%(agr)s, %(abr)s &nbsp;(%(ogr)s, %(obr)s)</td></tr>
      </tbody>
    </table>
  </div>
  <div class="note"><b>What this means.</b> Upstream's white strip is a flat line at the ceiling: every column the same, so the shading table has nothing to flatten and the lamp's fall-off is left in the picture. The patched strip spans a real profile (about 23&nbsp;%% darker at the edges, close to the 1.25&ndash;1.3&times; seen with the other backend) and the table can correct it. The output is less bright and less green, and its colour ratios (G/R %(agr)s, B/R %(abr)s) are close to the other backend's (%(ogr)s, %(obr)s).</div>

  <h2>4 &middot; Detail at 3600 dpi</h2>
  <p class="sub">The same physical area cut from each 3600 dpi scan (1000&times;750 pixels), lined up on the film's own corner. Drag to wipe between them.</p>
  <div class="crops">%(crops)s</div>
  <div class="note"><b>Sharpness is unchanged.</b> The patches do not touch the optics or the scanning speed: the same grain and the same dust specks appear in both. The change is in what the image is made of around them: the calibration, the area and the colour balance. The extra detail you gain is the film that was previously outside the window.</div>

  <h2>5 &middot; Infrared, as a second frame</h2>
  <p class="sub">With <code>--infra-red</code> set the driver delivers the image as two frames: the colour scan, then an infrared plane of the same area. Upstream has no infrared at all for this scanner. This is one two-frame scan; both views are aligned as the driver delivered them, with no registration applied.</p>
<pre>sane_control_option(h, infra-red, yes)
before start:  format=RGB   last_frame=0   864x640
frame 1:       format=RGB   last_frame=0   864x640  depth=16   (read to the end)
frame 2:       format=GRAY  last_frame=1   864x640  depth=16   (second sane_start, no sane_cancel between)</pre>
  <div class="toolbar"><span class="hint">Drag to wipe between the colour frame and its infrared frame, whole frame at 600 dpi.</span></div>
  <div class="stage" id="irStage" style="aspect-ratio:864/640">
    <div class="layer base"><img class="cimg" alt="Colour frame"></div>
    <div class="layer top"><img class="iimg" alt="Infrared frame"></div>
    <span class="tag o">infrared</span><span class="tag g">image</span>
    <div class="handle" tabindex="0" role="slider" aria-label="Wipe position, infrared" aria-valuemin="0" aria-valuemax="100" aria-valuenow="50"></div>
  </div>
  <p class="hint">In the infrared frame the film dye disappears and only physical defects remain: dust, hairs and scratches. The dark edge at the left is the film holder.</p>
  <div class="crops" style="margin-top:18px">%(irs)s</div>
  <div class="note"><b>How well do the frames line up?</b> At 3600&nbsp;dpi the scratches and dust in the infrared frame coincide with the visible ones to within 2&nbsp;pixels (0.015&nbsp;mm) in five regions of this scan (cross-correlation of the visible defects against the infrared defect map; vertical offsets &minus;0.3 to +0.6 pixel, horizontal &minus;0.9 to +1.8). The two frames are two passes of the carriage, and separate scans of this scanner repeat less well (about 0.1&nbsp;mm), so the man page tells frontends to verify the alignment rather than assume it.</div>

  <h2>6 &middot; The patches</h2>
  <p class="sub">Seven patches against SANE master <code>982deaef</code>. Apply them with <code>git am docs/upstream-genesys-patches/*.patch</code>. Each is a separate commit; the series builds and passes upstream's genesys unit tests.</p>
  <div class="card" style="padding:6px 8px"><table>
    <thead><tr><th>#</th><th>Patch</th><th>Lines</th><th></th></tr></thead>
    <tbody>%(patches)s</tbody></table></div>

  <h2>7 &middot; What is not fixed</h2>
  <ul class="caveats">
    <li><b>Speed.</b> At 3600&nbsp;dpi genesys takes about 53&nbsp;s against 34.6&nbsp;s for plustek8200i. It samples the film twice per output line and averages, following SilverFast's captured motor ramp. Halving the line count alone changed the vertical scale instead of the time, so the motor and line-period registers would have to be worked out again.</li>
    <li><b>Exposure multiplier</b> (<code>--exposure</code>) for dense negatives, for the same reason.</li>
    <li><b>Dust removal and white balance</b> (<code>--ir-clean</code>, <code>--auto-wb</code>) are host-side processing that genesys has no place for. <code>--infra-red</code> gives a frontend what it needs to do dust removal itself.</li>
    <li><b>Buttons.</b> The option names and the endpoint read are verified; a physical press through genesys is not.</li>
    <li><b>Other scanners.</b> <code>--infra-red</code> is written for every genesys model with an infrared scan method (CanoScan 8400F and 8600F, OpticFilm 7200i, 7500i, 8200i) but was tested only on the 8200i SE.</li>
    <li><b>One unit, one frame.</b> These are single-scanner, single-frame results.</li>
  </ul>

  <footer>
    <p><b>How this was made.</b> Same frame in the holder, scanned on 2026-10-21 with an OpticFilm 8200i (USB 07b3:1825). Upstream: SANE master <code>982deaef</code>, <code>genesys</code> only. Patched: the same tree with the seven patches (<code>%(lastcommit)s</code>). Both raw 16-bit scans went through the same <code>nv-cli render</code> negative conversion with each scan's own film-base estimate and no dust removal. The detail crops are aligned on the film's measured top-left corner. Margins are measured on the raw scans; the infrared frames come from a separate two-frame scan of the same frame. The page is built by <code>tools/build-genesys-patches-page.py</code>.</p>
    <p><b>Where to go next.</b> The patches, the <a href="drivers-comparison.html">earlier comparison</a> and the plustek8200i backend live in the
    <a href="https://github.com/cschalle/OpticFilm8200i-sane">OpticFilm8200i-sane repository</a>. Upstreaming is tracked in SANE backends
    <a href="https://gitlab.com/sane-project/backends/-/work_items/886">work item #886</a>.</p>
  </footer>
</div>

<script>
const IMG = %(imgjson)s;
const $ = (s, r=document) => r.querySelector(s);
const $$ = (s, r=document) => [...r.querySelectorAll(s)];
$$('.uimg').forEach(i => i.src = IMG.upstream);
$$('.pimg').forEach(i => i.src = IMG.patched);
$$('.cimg').forEach(i => i.src = IMG.colour600);
$$('.iimg').forEach(i => i.src = IMG.ir600);
$$('.stage[data-a]').forEach(st => { const a = $('.base img', st), b = $('.top img', st); a.src = IMG[st.dataset.a]; b.src = IMG[st.dataset.b]; });

/* canvas geometry: the whole-frame views are drawn on a shared film coordinate system */
const place = (sel, d) => $$(sel).forEach(i => Object.assign(i.style, {left:d.left+'%%', top:d.top+'%%', width:d.width+'%%', height:d.height+'%%'}));
place('.frame .uimg', %(placeU)s);
place('.frame .pimg', %(placeP)s);
$$('.frame .stage').forEach(s => { s.style.aspectRatio = '%(ASPECT)s'; });

function wipe(root){
  const top = $('.layer.top', root), h = $('.handle', root); let pos = 50;
  const set = p => { pos = Math.max(0, Math.min(100, p)); top.style.clipPath = `inset(0 ${100-pos}%%  0 0)`; h.style.left = pos+'%%'; h.setAttribute('aria-valuenow', Math.round(pos)); };
  const ev = e => { const r = root.getBoundingClientRect(); set((e.clientX - r.left) / r.width * 100); };
  root.addEventListener('pointerdown', e => { root.setPointerCapture(e.pointerId); root.dataset.drag = 1; ev(e); });
  root.addEventListener('pointermove', e => { if (root.dataset.drag) ev(e); });
  const end = () => delete root.dataset.drag;
  root.addEventListener('pointerup', end); root.addEventListener('pointercancel', end);
  h.addEventListener('keydown', e => { if (e.key==='ArrowLeft'){set(pos-2);e.preventDefault();} if (e.key==='ArrowRight'){set(pos+2);e.preventDefault();} });
  set(50);
}
wipe($('#wipeStage')); wipe($('#irStage')); $$('.crop .stage').forEach(wipe);

const frame = $('#frame'); let mode = 'wipe', zoom = 1;
function setMode(m){
  mode = m;
  $$('#modes button').forEach(b => b.setAttribute('aria-pressed', b.dataset.mode===m));
  ['wipe','side','blink'].forEach(k => $('#m-'+k).hidden = (k!==m));
  $('#zoomSeg').hidden = (m!=='side'); $('#autoBlinkLbl').hidden = (m!=='blink');
  if (m!=='blink') stopAuto();
}
$$('#modes button').forEach(b => b.addEventListener('click', () => setMode(b.dataset.mode)));
$('#chkWin').addEventListener('change', e => frame.classList.toggle('show-win', e.target.checked));
$('#chkFilm').addEventListener('change', e => frame.classList.toggle('show-film', e.target.checked));

const vpU = $('#vpU'), vpP = $('#vpP'), stU = $('#stU'), stP = $('#stP');
function setZoom(z){ zoom = z; [stU, stP].forEach(s => s.style.width = (z*100)+'%%'); $$('#zoomSeg button').forEach(b => b.setAttribute('aria-pressed', +b.dataset.z===z)); }
$$('#zoomSeg button').forEach(b => b.addEventListener('click', () => setZoom(+b.dataset.z)));
let lock = false;
const sync = (a, b) => a.addEventListener('scroll', () => { if (lock) return; lock = true; b.scrollLeft = a.scrollLeft; b.scrollTop = a.scrollTop; requestAnimationFrame(() => lock = false); });
sync(vpU, vpP); sync(vpP, vpU);

const blinkTop = $('#blinkTop'), blinkTag = $('#blinkTag'); let showPatched = true, timer = null;
function flip(v){ showPatched = (v===undefined) ? !showPatched : v; blinkTop.style.visibility = showPatched ? 'visible' : 'hidden'; blinkTag.textContent = showPatched ? 'patched' : 'upstream'; blinkTag.style.background = showPatched ? 'var(--ours)' : 'var(--gen)'; blinkTag.style.color = showPatched ? '#fff' : '#1a1200'; }
$('#blinkStage').addEventListener('click', () => { stopAuto(); flip(); });
$('#blinkStage').addEventListener('keydown', e => { if (e.key===' '){ e.preventDefault(); stopAuto(); flip(); } });
function stopAuto(){ if (timer){ clearInterval(timer); timer = null; } $('#autoBlink').checked = false; }
$('#autoBlink').addEventListener('change', e => { if (e.target.checked){ timer = setInterval(() => flip(), 800); } else stopAuto(); });
flip(true);

(function(){
  location.hash.slice(1).split(',').filter(Boolean).forEach(k => {
    if (['wipe','side','blink'].includes(k)) setMode(k);
    else if (k==='win'){ $('#chkWin').checked = true; frame.classList.add('show-win'); }
    else if (k==='film'){ $('#chkFilm').checked = true; frame.classList.add('show-film'); }
    else if (/^z[123]$/.test(k)) setZoom(+k[1]);
    else if (k==='off') flip(false);
  });
})();
</script>
</body>
</html>
"""

def rgb(m): return " / ".join("{:,}".format(v) for v in m["med"])
def wcell(i):
    ch, mn, mean, mx, ceil, n = white_after[i]
    return "%s&ndash;%s &nbsp;(%d / %s)" % (f"{int(mn):,}", f"{int(mx):,}", int(ceil), n)

place_d = lambda d: json.dumps({k: round(v, 4) for k, v in d.items()})
lastcommit = "63d8cb16"
vals = dict(
    STYLE=STYLE, PU=PU, PP=PP, overlays=overlays, ASPECT=ASPECT,
    bw=before["w"], bh=before["h"], aw=after["w"], ah=after["h"], ow=ours3600["w"], oh=ours3600["h"],
    m600u=margin_row(PU, "before_600"), m600p=margin_row(PP, "after_600"),
    m3600u=margin_row(PU, "before_3600"), m3600p=margin_row(PP, "after_3600"),
    fw=film_w, fh=film_h,
    wR=wcell(0), wG=wcell(1), wB=wcell(2), codes=", ".join(gain_codes), lit=", ".join("{:,}".format(int(v)) for v in lit),
    bmed=rgb(before), amed=rgb(after), omed=rgb(ours3600),
    bgr=before["gr"], bbr=before["br"], agr=after["gr"], abr=after["br"], ogr=ours3600["gr"], obr=ours3600["br"],
    crops=crop_html, irs=ir_html, patches="".join(patch_rows), lastcommit=lastcommit,
    imgjson=json.dumps(IMG), placeU=place_d(pb), placeP=place_d(pa),
)
open(OUT, "w").write(HTML % vals)
print(OUT, "%.1f MB" % (os.path.getsize(OUT) / 1e6))
