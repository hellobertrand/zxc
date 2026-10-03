#!/usr/bin/env python3
# Generates docs/images/bench-scatter.svg: decompression speed vs compressed size, ZXC vs the LZ4
# family, one panel per reference CPU. No dependencies; the SVG follows the viewer's light/dark theme.
# Data: docs/WHITEPAPER.md §7.5 (lzbench 2.3.1, Silesia, single thread).
#
#   python3 docs/images/bench-scatter.py > docs/images/bench-scatter.svg

ZXC_VERSION = "0.14.1"
LZ4_VERSION = "1.10.0"

# Ratio (%) per entry, identical on every machine.
ZXC_RATIO = [61.76, 53.86, 46.09, 42.99, 40.43, 36.28, 33.09]          # levels -1 .. -7
LZ4_RATIO = [62.15, 47.60, 36.75]                                      # --fast -17, default, hc -9
LZ4_NAMES = ["lz4 --fast", "lz4", "lz4hc -9"]
PAIRS = [(0, 0), (2, 1), (5, 2)]                                       # (zxc level idx, lz4 idx)

# Decompression speed (MB/s).
MACHINES = [
    ("Apple M2", "ARM64", [13524, 11338, 8356, 7906, 7394, 6740, 4628], [5166, 4770, 4503]),
    ("Google Axion", "ARM64", [9487, 7834, 5980, 5675, 5310, 4787, 3186], [4940, 4256, 3843]),
    ("AMD EPYC Zen 5", "x86_64", [11377, 10243, 6730, 6357, 5970, 5675, 4149], [5179, 4938, 4766]),
    ("AMD EPYC Zen 3", "x86_64", [8106, 6746, 4752, 4562, 4403, 4101, 2840], [4486, 3882, 3725]),
]

W, H = 960, 730
PW, PH = 420, 222                     # plot area of one panel
ORIGINS = [(70, 160), (520, 160), (70, 470), (520, 470)]
X_MIN, X_MAX = 30.0, 66.0             # ratio %
Y_MAX = 15.0                          # GB/s

out = []
emit = out.append


def sx(ox, r):
    return ox + (r - X_MIN) / (X_MAX - X_MIN) * PW


def sy(oy, mbps):
    return oy + PH - (mbps / 1000.0) / Y_MAX * PH


def text(x, y, s, cls, anchor="start"):
    emit(f'<text x="{x:.1f}" y="{y:.1f}" class="{cls}" text-anchor="{anchor}">{s}</text>')


emit(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
     f'role="img" aria-label="Decompression speed versus compressed size: ZXC is faster than the '
     f'LZ4 family at an equal or smaller size on four CPUs">')
emit("""<style>
svg{--bg:#ffffff;--fg:#1f2328;--mute:#656d76;--grid:#e6e8eb;--axis:#afb8c1;--zxc:#0969da;--lz4:#8c959f;--badge:#ddf4ff}
@media (prefers-color-scheme:dark){svg{--bg:#0d1117;--fg:#e6edf3;--mute:#8d96a0;--grid:#21262d;--axis:#3d444d;--zxc:#4493f8;--lz4:#6e7681;--badge:#0c2d6b}}
text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI","Noto Sans",Helvetica,Arial,sans-serif;fill:var(--fg)}
.bg{fill:var(--bg)}
.t{font-size:24px;font-weight:700}
.st{font-size:14px;fill:var(--mute)}
.pt{font-size:15px;font-weight:600}
.pa{font-size:13px;fill:var(--mute)}
.tk{font-size:11px;fill:var(--mute)}
.ax{font-size:12px;fill:var(--mute)}
.lv{font-size:11px;font-weight:600;fill:var(--zxc)}
.ln{font-size:11px;fill:var(--mute)}
.gain{font-size:11px;font-weight:600;fill:var(--zxc)}
.bd{font-size:13px;font-weight:700;fill:var(--zxc)}
.lg{font-size:13px}
.grid{stroke:var(--grid);stroke-width:1}
.axis{stroke:var(--axis);stroke-width:1}
.zl{fill:none;stroke:var(--zxc);stroke-width:2.5;stroke-linejoin:round}
.zp{fill:var(--zxc);stroke:var(--bg);stroke-width:1.5}
.ll{fill:none;stroke:var(--lz4);stroke-width:2;stroke-dasharray:5 4}
.lp{fill:var(--lz4);stroke:var(--bg);stroke-width:1.5}
.cn{stroke:var(--zxc);stroke-width:1;stroke-dasharray:2 3;opacity:.7}
.bdr{fill:var(--badge)}
</style>""")
emit(f'<rect class="bg" width="{W}" height="{H}" rx="8"/>')

text(40, 48, "Faster than LZ4, at a smaller size, on every CPU", "t")
text(40, 74, "Decompression speed vs compressed size · Silesia corpus · single thread · "
             "lzbench 2.3.1, -march=native", "st")

# Legend
emit(f'<path class="zl" d="M40 104h28"/><circle class="zp" cx="54" cy="104" r="5"/>')
text(76, 108, f"ZXC {ZXC_VERSION}, levels -1 to -7", "lg")
emit(f'<path class="ll" d="M330 104h28"/><rect class="lp" x="339" y="99" width="10" height="10"/>')
text(366, 108, f"LZ4 {LZ4_VERSION}: --fast -17, default, HC -9", "lg")
text(W - 40, 108, "↖ up and left is better", "st", "end")

for k, ((name, arch, zxc, lz4), (ox, oy)) in enumerate(zip(MACHINES, ORIGINS)):
    # Grid and axes
    for g in range(0, int(Y_MAX), 2):
        y = sy(oy, g * 1000)
        emit(f'<path class="grid" d="M{ox} {y:.1f}h{PW}"/>')
        text(ox - 8, y + 4, f"{g}", "tk", "end")
    for r in range(30, 70, 5):
        if r < X_MIN or r > X_MAX:
            continue
        x = sx(ox, r)
        emit(f'<path class="grid" d="M{x:.1f} {oy}v{PH}"/>')
        text(x, oy + PH + 16, f"{r}%", "tk", "middle")
    emit(f'<path class="axis" d="M{ox} {oy + PH}h{PW}"/>')
    if k % 2 == 0:
        emit(f'<text class="ax" transform="translate({ox - 38} {oy + PH / 2}) rotate(-90)" '
             f'text-anchor="middle">Decompression (GB/s)</text>')
    if k >= 2:
        text(ox + PW, oy + PH + 36, "Compressed size (% of original, smaller ←)", "ax", "end")

    # Title and headline gain
    emit(f'<text x="{ox}" y="{oy - 14}" class="pt">{name} <tspan class="pa">{arch}</tspan></text>')
    best = max(zxc[z] / lz4[l] for z, l in PAIRS)
    label = f"up to {best:.1f}× faster"
    bw = 8 + 7.4 * len(label)
    emit(f'<rect class="bdr" x="{ox + PW - bw:.1f}" y="{oy - 32}" width="{bw:.1f}" height="24" rx="12"/>')
    text(ox + PW - bw / 2, oy - 15, label, "bd", "middle")

    # Matched-tier connectors; the speedup is printed under the LZ4 point
    for z, l in PAIRS:
        x1, y1 = sx(ox, LZ4_RATIO[l]), sy(oy, lz4[l])
        x2, y2 = sx(ox, ZXC_RATIO[z]), sy(oy, zxc[z])
        emit(f'<path class="cn" d="M{x1:.1f} {y1:.1f}L{x2:.1f} {y2:.1f}"/>')
        text(x1, y1 + 33, f"ZXC ×{zxc[z] / lz4[l]:.2f}", "gain", "middle")

    # LZ4 family
    pts = " ".join(f"{sx(ox, r):.1f},{sy(oy, v):.1f}" for r, v in zip(LZ4_RATIO, lz4))
    emit(f'<polyline class="ll" points="{pts}"/>')
    for r, v, n in zip(LZ4_RATIO, lz4, LZ4_NAMES):
        x, y = sx(ox, r), sy(oy, v)
        emit(f'<rect class="lp" x="{x - 5:.1f}" y="{y - 5:.1f}" width="10" height="10"/>')
        text(x, y + 19, n, "ln", "middle")

    # ZXC levels
    pts = " ".join(f"{sx(ox, r):.1f},{sy(oy, v):.1f}" for r, v in zip(ZXC_RATIO, zxc))
    emit(f'<polyline class="zl" points="{pts}"/>')
    for i, (r, v) in enumerate(zip(ZXC_RATIO, zxc)):
        x, y = sx(ox, r), sy(oy, v)
        emit(f'<circle class="zp" cx="{x:.1f}" cy="{y:.1f}" r="5"/>')
        text(x, y - 10, f"-{i + 1}", "lv", "middle")

emit("</svg>")
print("\n".join(out))
