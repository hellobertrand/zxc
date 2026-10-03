#!/usr/bin/env python3
# Generates the benchmark charts of the README and the whitepaper, next to this script:
#
#   bench-scatter.svg                decompression speed vs size, ZXC vs its closest competitors
#   bench-pareto-decompression.svg   ratio vs decompression speed, all codecs (lzbench axes)
#   bench-pareto-compression.svg     ratio vs compression speed, all codecs (lzbench axes)
#   bench-effective.svg              ratio-normalized decode throughput vs LZ4
#   bench-cycles.svg                 decode cost in CPU cycles per byte
#   bench-arm64.svg                  speed and size relative to LZ4, all codecs, Apple M2
#
# The numbers are read from the lzbench tables of docs/WHITEPAPER.md §7.5, so updating those
# tables and re-running this script is all a new benchmark run needs. No dependencies; every SVG
# carries its own light and dark colours (prefers-color-scheme).
#
#   python3 docs/images/bench-charts.py

import math
import pathlib
import re

HERE = pathlib.Path(__file__).resolve().parent
WHITEPAPER = HERE.parent / "WHITEPAPER.md"

# Whitepaper section, panel title, architecture, clock in GHz (for cycles per byte).
MACHINES = [
    ("7.5.1", "Apple M2", "ARM64", 3.5),
    ("7.5.2", "Google Axion", "ARM64", 2.6),
    ("7.5.3", "AMD EPYC Zen 5", "x86_64", 2.1),
    ("7.5.4", "AMD EPYC Zen 3", "x86_64", 2.2),
]

ZXC = [f"zxc -{i}" for i in range(1, 8)]
LZ4 = ["lz4 --fast -17", "lz4", "lz4hc -9"]
ZSTD = ["zstd --fast --1", "zstd -1", "zstd -3"]
OTHER = ["lzav -1", "snappy", "zlib -1"]
SHORT = {"lz4 --fast -17": "lz4 --fast", "zstd --fast --1": "zstd --fast", "lzav -1": "lzav",
         "zlib -1": "zlib"}
PAIRS = [("zxc -1", "lz4 --fast -17"), ("zxc -3", "lz4"), ("zxc -6", "lz4hc -9")]
ZSTD_PAIR = ("zxc -7", "zstd -1")

COMP, DEC, RATIO = 0, 1, 2


def load():
    """[{codec: (compression MB/s, decompression MB/s, ratio %)}] per machine, plus versions."""
    text = WHITEPAPER.read_text(encoding="utf-8")
    machines, versions = [], {}
    for sec, _, _, _ in MACHINES:
        start = text.index(f"#### {sec} ")
        body = text[start:start + 1 + re.search(r"\n###+ ", text[start + 1:]).start()]
        rows = {}
        for line in body.splitlines():
            c = [x.strip().strip("*").strip() for x in line.strip().strip("|").split("|")]
            if len(c) < 5 or not c[1].endswith("MB/s"):
                continue
            tok = c[0].split()
            rows[" ".join(tok[:1] + tok[2:])] = (float(c[1].split()[0]), float(c[2].split()[0]),
                                                 float(c[4]))
            if len(tok) > 1:
                versions[tok[0]] = tok[1]
        assert all(k in rows for k in ZXC + LZ4 + ZSTD + OTHER + ["memcpy"]), sec
        machines.append(rows)
    return machines, versions


# ---------------------------------------------------------------------------------------------
# SVG plumbing

# role: (light, dark). Surfaces match GitHub's so the charts sit flush in the README.
ROLES = {
    "bg": ("#ffffff", "#0d1117"), "fg": ("#1f2328", "#e6edf3"), "mute": ("#656d76", "#8d96a0"),
    "grid": ("#e6e8eb", "#21262d"), "axis": ("#afb8c1", "#3d444d"),
    "zxc": ("#0969da", "#4493f8"), "zstd": ("#bc4c00", "#db6d28"), "gray": ("#8c959f", "#6e7681"),
    "wash": ("#ddf4ff", "#0c2d6b"), "front": ("#d0d7de", "#30363d"), 
}
# selector, property, role
COLOURS = [
    (".bg", "fill", "bg"), ("text", "fill", "fg"), (".mu", "fill", "mute"), (".fg", "fill", "fg"),
    (".grid", "stroke", "grid"), (".axis", "stroke", "axis"),
    (".zl", "stroke", "zxc"), (".zp", "fill", "zxc"), (".cn", "stroke", "zxc"),
    (".gl", "stroke", "gray"), (".gp", "fill", "gray"),
    (".ol", "stroke", "zstd"), (".op", "fill", "zstd"),
    (".rp", "fill", "fg"), (".ring", "stroke", "bg"), (".wash", "fill", "wash"), (".fr", "stroke", "front"),
    (".halo", "stroke", "bg"),
]
STATIC = (
    'text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",Helvetica,Arial,'
    'sans-serif;font-size:11px}'
    '.t{font-size:24px;font-weight:700}.st{font-size:14px}.pt{font-size:15px;font-weight:600}'
    '.pa{font-size:13px;font-weight:400}.ax{font-size:12px}.b{font-weight:600}'
    '.bd{font-size:13px;font-weight:700}.lg{font-size:13px}.num{font-variant-numeric:tabular-nums}'
    '.grid,.axis{stroke-width:1}.ring{stroke-width:1.5}'
    '.halo{paint-order:stroke;stroke-width:4;stroke-linejoin:round}'
    '.zl{fill:none;stroke-width:2.5;stroke-linejoin:round}'
    '.gl{fill:none;stroke-width:1.5;stroke-dasharray:5 4}'
    '.ol{fill:none;stroke-width:1.5;stroke-dasharray:5 4}'
    '.cn{stroke-width:1;stroke-dasharray:2 3;opacity:.7}'
    '.fr{fill:none;stroke-width:14;stroke-linejoin:round;stroke-linecap:round}'
)


def style():
    light = "".join(f"{s}{{{p}:{ROLES[r][0]}}}" for s, p, r in COLOURS)
    dark = "".join(f"{s}{{{p}:{ROLES[r][1]}}}" for s, p, r in COLOURS)
    return f"<style>{STATIC}{light}@media (prefers-color-scheme:dark){{{dark}}}</style>"


def num(v):
    return f"{v:,.0f}".replace(",", " ")


class Chart:
    def __init__(self, w, h, label):
        self.w, self.h = w, h
        self.o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" '
                  f'height="{h}" role="img" aria-label="{label}">', style(),
                  f'<rect class="bg" width="{w}" height="{h}" rx="8"/>']

    def add(self, s):
        self.o.append(s)

    def text(self, x, y, s, cls="", anchor="start"):
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" class="{cls}" text-anchor="{anchor}">{s}</text>')

    def header(self, title, subtitle, hint=None):
        self.text(40, 48, title, "t")
        self.text(40, 74, subtitle, "st mu")
        if hint:
            self.text(self.w - 40, 74, hint, "st mu", "end")

    def mark(self, kind, x, y):
        if kind in ("zxc-bar", "gray-bar"):
            cls = "zp" if kind == "zxc-bar" else "gp"
            self.add(f'<rect class="{cls}" x="{x - 12:.1f}" y="{y - 6:.1f}" width="24" height="12" rx="3"/>')
        elif kind == "zxc":
            self.add(f'<circle class="zp ring" cx="{x:.1f}" cy="{y:.1f}" r="5"/>')
        elif kind == "lz4":
            self.add(f'<rect class="gp ring" x="{x - 5:.1f}" y="{y - 5:.1f}" width="10" height="10"/>')
        elif kind in ("zstd", "zstd-point"):
            self.add(f'<path class="op ring" d="M{x:.1f} {y - 6:.1f}l6 11h-12z"/>')
        else:
            self.add(f'<path class="gp ring" d="M{x:.1f} {y - 6:.1f}l6 6l-6 6l-6-6z"/>')

    def legend(self, items, y=104):
        x = 40
        for kind, label in items:
            if kind == "front":
                self.add(f'<path class="fr" d="M{x + 7} {y}h14"/>')
            else:
                line = {"zxc": "zl", "lz4": "gl", "zstd": "ol"}.get(kind)
                if line:
                    self.add(f'<path class="{line}" d="M{x} {y}h28"/>')
                self.mark(kind, x + 14, y)
            self.text(x + 36, y + 4, label, "lg")
            x += 36 + 7.2 * len(label) + 28

    def save(self, name):
        (HERE / name).write_text("\n".join(self.o + ["</svg>"]) + "\n", encoding="utf-8")


W = 960
PW = 420
GRID = [(70, 160), (520, 160), (70, 510), (520, 510)]     # panel origins of the 2x2 charts


def panel_title(c, ox, oy, name, arch):
    c.add(f'<text x="{ox}" y="{oy - 14}" class="pt">{name} <tspan class="pa mu">{arch}</tspan></text>')


# ---------------------------------------------------------------------------------------------
# bench-scatter.svg: linear speed vs size, each ZXC level tied to its closest competitor

def scatter(data, ver):
    ph = 262
    c = Chart(W, 810, "Decompression speed versus compressed size: ZXC is faster than the LZ4 "
                      "family and zstd -1 at an equal or smaller size on four CPUs")
    c.header("Faster than LZ4, at a smaller size, on every CPU",
             "Decompression speed vs compressed size · Silesia · single thread · lzbench 2.3.1",
             "↖ up and left is better")
    c.legend([("zxc", f"ZXC {ver['zxc']}, levels -1 to -7"),
              ("lz4", f"LZ4 {ver['lz4']}: --fast -17, default, HC -9"),
              ("zstd-point", f"zstd {ver['zstd']} -1")])
    x_min, x_max = 30.0, 66.0
    for k, (rows, (_, name, arch, _), (ox, oy)) in enumerate(zip(data, MACHINES, GRID)):
        # Own y scale per panel, from zero, with headroom for the level labels
        y_max = 2 * math.ceil(max(rows[z][DEC] for z in ZXC) * 1.1 / 2000)

        def X(r):
            return ox + (r - x_min) / (x_max - x_min) * PW

        def Y(v):
            return oy + ph - (v / 1000.0) / y_max * ph

        for g in range(0, y_max + 1, 2):
            c.add(f'<path class="grid" d="M{ox} {Y(g * 1000):.1f}h{PW}"/>')
            c.text(ox - 8, Y(g * 1000) + 4, f"{g}", "mu", "end")
        for r in range(30, 70, 5):
            c.add(f'<path class="grid" d="M{X(r):.1f} {oy}v{ph}"/>')
            c.text(X(r), oy + ph + 16, f"{r}%", "mu", "middle")
        c.add(f'<path class="axis" d="M{ox} {oy + ph}h{PW}"/>')
        if k % 2 == 0:
            c.add(f'<text class="ax mu" transform="translate({ox - 38} {oy + ph / 2}) rotate(-90)" '
                  f'text-anchor="middle">Decompression (GB/s)</text>')
        if k >= 2:
            c.text(ox + PW, oy + ph + 36, "Compressed size (% of original, smaller ←)", "ax mu", "end")

        panel_title(c, ox, oy, name, arch)
        gains = [rows[z][DEC] / rows[o][DEC] for z, o in PAIRS + [ZSTD_PAIR]]
        label = f"up to {max(gains):.1f}× faster"
        bw = 8 + 7.4 * len(label)
        c.add(f'<rect class="wash" x="{ox + PW - bw:.1f}" y="{oy - 32}" width="{bw:.1f}" height="24" rx="12"/>')
        c.text(ox + PW - bw / 2, oy - 15, label, "bd", "middle")

        # Matched-tier connectors; the speedup is printed under the LZ4 point
        for z, o in PAIRS:
            x1, y1 = X(rows[o][RATIO]), Y(rows[o][DEC])
            c.add(f'<path class="cn" d="M{x1:.1f} {y1:.1f}L{X(rows[z][RATIO]):.1f} {Y(rows[z][DEC]):.1f}"/>')
            c.text(x1, y1 + 33, f"ZXC ×{rows[z][DEC] / rows[o][DEC]:.2f}", "b", "middle")

        # zstd -1, labelled on its right: below it sits the x axis
        z, o = ZSTD_PAIR
        x1, y1 = X(rows[o][RATIO]), Y(rows[o][DEC])
        c.add(f'<path class="cn" d="M{x1:.1f} {y1:.1f}L{X(rows[z][RATIO]):.1f} {Y(rows[z][DEC]):.1f}"/>')
        c.mark("zstd", x1, y1)
        c.add(f'<text x="{x1 + 10:.1f}" y="{y1 + 4:.1f}" class="mu">zstd -1 <tspan class="b fg">'
              f'ZXC ×{rows[z][DEC] / rows[o][DEC]:.2f}</tspan></text>')

        pts = " ".join(f"{X(rows[o][RATIO]):.1f},{Y(rows[o][DEC]):.1f}" for o in LZ4)
        c.add(f'<polyline class="gl" points="{pts}"/>')
        for o in LZ4:
            c.mark("lz4", X(rows[o][RATIO]), Y(rows[o][DEC]))
            c.text(X(rows[o][RATIO]), Y(rows[o][DEC]) + 19, SHORT.get(o, o), "mu", "middle")

        pts = " ".join(f"{X(rows[z][RATIO]):.1f},{Y(rows[z][DEC]):.1f}" for z in ZXC)
        c.add(f'<polyline class="zl" points="{pts}"/>')
        for z in ZXC:
            c.mark("zxc", X(rows[z][RATIO]), Y(rows[z][DEC]))
            c.text(X(rows[z][RATIO]), Y(rows[z][DEC]) - 10, z[4:], "b", "middle")
    c.save("bench-scatter.svg")


# ---------------------------------------------------------------------------------------------
# bench-pareto-*.svg: the lzbench view. Speed on a log x axis, ratio on y with the best at the top.

def frontier(rows, metric):
    """Codecs no other codec beats on both speed and ratio, fastest first."""
    out, best = [], math.inf
    for k in sorted((k for k in rows if k != "memcpy"), key=lambda k: -rows[k][metric]):
        if rows[k][RATIO] < best:
            out.append(k)
            best = rows[k][RATIO]
    return out


def pareto(data, ver, metric, fname, title, subtitle, x0, x1, ticks, place):
    ph = 270
    what = "Compression" if metric == COMP else "Decompression"
    c = Chart(W, 846, f"{what} speed versus compression ratio for ZXC, LZ4, zstd, lzav, snappy "
                      f"and zlib on four CPUs, with the Pareto frontier")
    c.header(title, subtitle, "↗ up and right is better")
    c.legend([("zxc", f"ZXC {ver['zxc']}"), ("lz4", f"LZ4 {ver['lz4']}"),
              ("zstd", f"zstd {ver['zstd']}"), ("other", "lzav, snappy, zlib"),
              ("front", "Pareto frontier")])
    y0, y1 = 28.0, 66.0
    for k, (rows, (_, name, arch, _), (ox, oy)) in enumerate(zip(data, MACHINES, GRID)):
        def X(v):
            return ox + (math.log10(v) - math.log10(x0)) / (math.log10(x1) - math.log10(x0)) * PW

        def Y(r):       # smaller ratio (better) at the top
            return oy + (r - y0) / (y1 - y0) * ph

        def P(key):
            return X(rows[key][metric]), Y(rows[key][RATIO])

        for t in ticks:
            c.add(f'<path class="grid" d="M{X(t):.1f} {oy}v{ph}"/>')
            c.text(X(t), oy + ph + 16, num(t), "mu num", "middle")
        for r in range(30, 70, 10):
            c.add(f'<path class="grid" d="M{ox} {Y(r):.1f}h{PW}"/>')
            c.text(ox - 8, Y(r) + 4, f"{r}%", "mu num", "end")
        c.add(f'<path class="axis" d="M{ox} {oy + ph}h{PW}"/>')
        if k % 2 == 0:
            c.add(f'<text class="ax mu" transform="translate({ox - 42} {oy + ph / 2}) rotate(-90)" '
                  f'text-anchor="middle">Compression ratio, % (lower is better ↑)</text>')
        if k >= 2:
            c.text(ox + PW, oy + ph + 36, f"{what} speed, MB/s (log scale) → faster", "ax mu", "end")
        panel_title(c, ox, oy, name, arch)

        front = frontier(rows, metric)
        c.add('<polyline class="fr" points="' + " ".join("%.1f,%.1f" % P(f) for f in front) + '"/>')
        for kind, line, family in (("lz4", "gl", LZ4), ("zstd", "ol", ZSTD), ("other", None, OTHER)):
            if line:
                c.add(f'<polyline class="{line}" points="' + " ".join("%.1f,%.1f" % P(f) for f in family) + '"/>')
            for key in family:
                x, y = P(key)
                c.mark(kind, x, y)
                dx, dy, anchor = place.get(key, (9, 4, "start"))
                c.text(x + dx, y + dy, SHORT.get(key, key), "mu", anchor)
        c.add('<polyline class="zl" points="' + " ".join("%.1f,%.1f" % P(z) for z in ZXC) + '"/>')
        for z in ZXC:
            x, y = P(z)
            c.mark("zxc", x, y)
            dx, dy, anchor = place.get(z, (7, -8, "start"))
            c.text(x + dx, y + dy, z[4:], "b", anchor)
    c.save(fname)


# ---------------------------------------------------------------------------------------------
# bench-effective.svg, bench-cycles.svg: one row per codec, one column of bars per CPU

def bars(fname, aria, title, subtitle, hint, codecs, values, x_max, ticks, unit, ref=None, clock=False):
    left, colw, barw, rowh = 150, 196, 132, 24
    top = 164
    gap = 10                                              # between ZXC and the other codecs
    height = top + len(codecs) * rowh + gap + 50
    c = Chart(W, height, aria)
    c.header(title, subtitle, hint)
    c.legend([("zxc-bar", "ZXC"), ("gray-bar", "other codecs")])
    bottom = top + len(codecs) * rowh + gap

    def row_y(i):
        return top + i * rowh + (gap if codecs[i] not in ZXC else 0)

    for i, key in enumerate(codecs):
        c.text(left - 14, row_y(i) + 16, SHORT.get(key, key), "lg b" if key in ZXC else "lg mu", "end")
    for m, (_, name, arch, ghz) in enumerate(MACHINES):
        ox = left + m * colw
        panel_title(c, ox, top - 6, name, f"{ghz} GHz" if clock else arch)
        for t in ticks:
            x = ox + t / x_max * barw
            c.add(f'<path class="{"axis" if t in (0, ref) else "grid"}" d="M{x:.1f} {top}v{bottom - top}"/>')
            c.text(x, bottom + 16, f"{t}{unit}" if t != ref else f"{t}{unit} = LZ4", "mu num", "middle")
        for i, key in enumerate(codecs):
            v = values[m][key]
            y, w = row_y(i) + 5, max(2.0, v / x_max * barw)
            r = min(3.0, w / 2)
            cls = "zp" if key in ZXC else "gp"
            c.add(f'<path class="{cls}" d="M{ox} {y}h{w - r:.1f}a{r} {r} 0 0 1 {r} {r}v{14 - 2 * r}'
                  f'a{r} {r} 0 0 1 -{r} {r}h-{w - r:.1f}z"/>')
            c.text(ox + w + 6, y + 11, f"{v:.2f}{unit}", "num halo b" if key in ZXC else "num halo mu")
    c.save(fname)


def effective(data, ver):
    codecs = ZXC + ["lz4", "lz4 --fast -17", "lz4hc -9", "lzav -1", "snappy"] + ZSTD
    values = []
    for rows in data:
        base = rows["lz4"][DEC] / rows["lz4"][RATIO]
        values.append({k: rows[k][DEC] / rows[k][RATIO] / base for k in codecs})
    low = min(v[z] for v in values for z in ZXC)
    bars("bench-effective.svg",
         "Effective throughput relative to LZ4 for each codec on four CPUs: every ZXC level is above LZ4",
         "Every ZXC level delivers more data per compressed byte than LZ4",
         f"Effective throughput = decompression speed ÷ compression ratio, relative to LZ4 {ver['lz4']}"
         f" · Silesia · single thread",
         "→ longer is better", codecs, values, 2.4, [0, 1, 2], "×", ref=1)
    assert low > 1.0, low


def cycles(data, ver):
    codecs = ZXC + ["memcpy"] + LZ4 + ["lzav -1", "snappy"] + ZSTD
    values = [{k: ghz * 1000.0 / rows[k][DEC] for k in codecs}
              for rows, (_, _, _, ghz) in zip(data, MACHINES)]
    lo = min(v[z] for v in values for z in ZXC)
    hi = max(v[z] for v in values for z in ZXC)
    bars("bench-cycles.svg",
         "CPU cycles per decompressed byte for each codec on four CPUs",
         f"ZXC decodes a byte in {lo:.1f} to {hi:.1f} CPU cycles",
         "Cycles per byte = clock frequency ÷ decompression speed · Silesia · single thread",
         "← shorter is better", codecs, values, 2.4, [0, 1, 2], "", clock=True)


# ---------------------------------------------------------------------------------------------
# bench-arm64.svg: everything relative to LZ4, one machine

def relative(data, ver):
    rows = data[0]
    _, name, arch, _ = MACHINES[0]
    ox, oy, pw, ph = 80, 140, 840, 400
    x0, x1, y1 = 60.0, 136.0, 3.0
    c = Chart(W, 620, f"Decompression speed and compressed size of each codec relative to LZ4 on {name}")
    c.header(f"Speed and size relative to LZ4 · {name}",
             "Decompression speed vs compressed size, LZ4 = 1 · Silesia · single thread · lzbench 2.3.1",
             "↖ up and left is better")
    c.legend([("zxc", f"ZXC {ver['zxc']}"), ("lz4", f"LZ4 {ver['lz4']}"),
              ("zstd", f"zstd {ver['zstd']}"), ("other", "lzav, snappy, zlib")])
    base = rows["lz4"]

    def P(key):
        x = ox + (rows[key][RATIO] / base[RATIO] * 100 - x0) / (x1 - x0) * pw
        return x, oy + ph - rows[key][DEC] / base[DEC] / y1 * ph

    def X(v):
        return ox + (v - x0) / (x1 - x0) * pw

    def Y(v):
        return oy + ph - v / y1 * ph

    # The quadrant that beats LZ4 on both axes
    c.add(f'<rect class="wash" x="{ox}" y="{oy}" width="{X(100) - ox:.1f}" height="{Y(1) - oy:.1f}" opacity=".45"/>')
    c.text(ox + 12, oy + 22, "Faster and smaller than LZ4", "ax b")
    for g in (0, 0.5, 1, 1.5, 2, 2.5, 3):
        c.add(f'<path class="{"axis" if g == 1 else "grid"}" d="M{ox} {Y(g):.1f}h{pw}"/>')
        c.text(ox - 8, Y(g) + 4, f"{g:g}×", "mu num", "end")
    for v in range(60, 140, 10):
        c.add(f'<path class="{"axis" if v == 100 else "grid"}" d="M{X(v):.1f} {oy}v{ph}"/>')
        c.text(X(v), oy + ph + 16, f"{v}%", "mu num", "middle")
    c.add(f'<path class="axis" d="M{ox} {oy + ph}h{pw}"/>')
    c.add(f'<text class="ax mu" transform="translate({ox - 46} {oy + ph / 2}) rotate(-90)" '
          f'text-anchor="middle">Decompression speed vs LZ4</text>')
    c.text(ox + pw, oy + ph + 38, "Compressed size vs LZ4 (smaller ←)", "ax mu", "end")

    place = {"lz4 --fast -17": (0, -13, "middle"), "lz4hc -9": (0, 22, "middle"),
             "zstd -3": (0, -13, "middle"), "zstd -1": (0, 22, "middle")}
    for kind, line, family in (("lz4", "gl", LZ4), ("zstd", "ol", ZSTD), ("other", None, OTHER)):
        if line:
            c.add(f'<polyline class="{line}" points="' + " ".join("%.1f,%.1f" % P(f) for f in family) + '"/>')
        for key in family:
            x, y = P(key)
            if key == "lz4":        # the reference everything is measured against
                continue
            c.mark(kind, x, y)
            dx, dy, anchor = place.get(key, (10, 4, "start"))
            c.text(x + dx, y + dy, SHORT.get(key, key), "lg mu", anchor)
    x, y = P("lz4")
    c.add(f'<rect class="rp ring" x="{x - 8:.1f}" y="{y - 8:.1f}" width="16" height="16" rx="2"/>')
    c.text(x + 14, y + 20, "LZ4 (reference)", "pt")
    c.add('<polyline class="zl" points="' + " ".join("%.1f,%.1f" % P(z) for z in ZXC) + '"/>')
    for z in ZXC:
        x, y = P(z)
        c.mark("zxc", x, y)
        c.text(x - 4, y - 12, z, "lg b", "end")
    c.save("bench-arm64.svg")


def main():
    data, ver = load()
    scatter(data, ver)
    pareto(data, ver, DEC, "bench-pareto-decompression.svg",
           "Every ZXC level sits on the decompression Pareto frontier",
           "Compression ratio vs decompression speed · Silesia · single thread · lzbench 2.3.1",
           300, 20000, [500, 1000, 2000, 5000, 10000],
           {"lz4 --fast -17": (-9, 4, "end"), "lz4": (-9, 4, "end"), "lz4hc -9": (-9, 4, "end"),
            "lzav -1": (0, 20, "middle"), "snappy": (-9, 4, "end"), "zstd --fast --1": (-9, 4, "end"),
            "zstd -1": (-9, 4, "end"), "zstd -3": (-9, 4, "end")})
    for rows in data:
        assert all(z in frontier(rows, DEC) for z in ZXC)
    pareto(data, ver, COMP, "bench-pareto-compression.svg",
           "Compression speed is what ZXC trades away",
           "Compression ratio vs compression speed · Silesia · single thread · lzbench 2.3.1",
           5, 3000, [10, 30, 100, 300, 1000],
           {"lz4 --fast -17": (0, 20, "middle"), "lz4": (-9, 16, "end"), "lz4hc -9": (0, -11, "middle"),
            "lzav -1": (10, -1, "start"), "zstd --fast --1": (10, 13, "start")})
    effective(data, ver)
    cycles(data, ver)
    relative(data, ver)


if __name__ == "__main__":
    main()
