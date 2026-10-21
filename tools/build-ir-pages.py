#!/usr/bin/env python3
"""Build docs/comparison/the-fourth-channel.html and ...-review-notes.html from their Markdown.

The Markdown files are the source; the pages reuse the stylesheet of
docs/comparison/drivers-comparison.html, so they look like the other comparison pages.
Needs the Python 'markdown' package.

Run:  tools/build-ir-pages.py
"""
import os, re
import markdown

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CMP = os.path.join(ROOT, "docs", "comparison")
HP = "https://htmlpreview.github.io/?https://github.com/cschalle/OpticFilm8200i-sane/blob/main/docs/comparison/"
GH = "https://github.com/cschalle/OpticFilm8200i-sane/blob/main/docs/comparison/"

orig = open(os.path.join(CMP, "drivers-comparison.html")).read()
STYLE = re.search(r"<style>(.*?)</style>", orig, re.S).group(1)

EXTRA = """
.prose>*:not(.tw){max-width:84ch}
.prose h1{margin-top:.2em}
.prose h2{margin:44px 0 6px;padding-top:6px}
.prose h3{font-size:1.05rem;margin:26px 0 4px}
.prose p,.prose li{line-height:1.6}
.prose li{margin:.25em 0}
.prose code{background:var(--card);border:1px solid var(--border);border-radius:4px;padding:0 .3em;font-size:.88em}
.prose pre{background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:12px 16px;overflow:auto;line-height:1.5}
.prose pre code{background:none;border:0;padding:0}
.prose a{color:var(--accent)}
.prose blockquote{margin:1em 0;padding:.2em 1em;border-left:4px solid var(--accent);color:var(--muted)}
.eyebrow{color:var(--muted);font-size:.85em;letter-spacing:.04em;text-transform:uppercase;margin:0 0 4px}
.tw{max-width:100%;overflow-x:auto;margin:1em 0;background:var(--card);border:1px solid var(--border);border-radius:var(--radius);padding:4px 6px}
.tw table{min-width:640px;font-size:.9em}
.tw th,.tw td{text-align:left;vertical-align:top}
.tw td:first-child,.tw th:first-child{white-space:nowrap}
"""

PAGES = [
    ("the-fourth-channel", "The Fourth Channel: infrared in SANE"),
    ("the-fourth-channel-review-notes", "The Fourth Channel: verification notes"),
]


def links(html):
    html = html.replace('href="the-fourth-channel-review-notes.md"', 'href="%sthe-fourth-channel-review-notes.html"' % HP)
    html = html.replace('href="the-fourth-channel.md"', 'href="%sthe-fourth-channel.html"' % HP)
    html = html.replace('href="genesys-patches.html"', 'href="%sgenesys-patches.html"' % HP)
    html = html.replace('href="drivers-comparison.html"', 'href="%sdrivers-comparison.html"' % HP)
    return html


for name, title in PAGES:
    src = open(os.path.join(CMP, name + ".md")).read()
    eyebrow = ""
    m = re.match(r"([^\n#][^\n]*)\n\n(?=#)", src)
    if m:
        eyebrow, src = m.group(1), src[m.end():]
    body = markdown.markdown(src, extensions=["tables", "fenced_code", "sane_lists", "toc"])
    body = re.sub(r"<table>", '<div class="tw"><table>', body)
    body = body.replace("</table>", "</table></div>")
    body = links(body)
    page = """<!doctype html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>%(title)s</title>
<style>%(STYLE)s
%(EXTRA)s</style></head>
<body>
<div class="wrap">
  <nav class="links" aria-label="Project links">
    <a class="btn primary" href="https://github.com/cschalle/OpticFilm8200i-sane">The plustek8200i backend <span class="where">on GitHub</span></a>
    <a class="btn" href="%(HP)sgenesys-patches.html">Genesys patches <span class="where">before and after</span></a>
    <a class="btn" href="%(HP)sdrivers-comparison.html">Earlier comparison <span class="where">plustek8200i vs genesys</span></a>
    <a class="btn" href="https://gitlab.com/sane-project/backends/-/work_items/886">Upstream SANE ticket #886 <span class="where">on GitLab</span></a>
    <a class="btn" href="%(GH)s%(name)s.md">Markdown source <span class="where">on GitHub</span></a>
  </nav>
  <div class="prose">
  <p class="eyebrow">%(eyebrow)s</p>
%(body)s
  </div>
</div>
</body></html>
""" % dict(title=title, STYLE=STYLE, EXTRA=EXTRA, HP=HP, GH=GH, name=name, eyebrow=eyebrow, body=body)
    out = os.path.join(CMP, name + ".html")
    open(out, "w").write(page)
    print(out, "%.0f kB" % (os.path.getsize(out) / 1e3))
