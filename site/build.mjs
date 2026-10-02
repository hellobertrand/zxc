#!/usr/bin/env node
// Static site generator for libzxc.org.
//
//   node build.mjs            -> writes ./dist
//   node build.mjs --serve    -> builds, then serves ./dist on http://localhost:8080
//
// Inputs read from the zxc repository (the parent folder, or REPO_ROOT), so the
// site never drifts from the code:
//   include/zxc_constants.h   library version
//   docs/*.md, docs/man/*.md  documentation pages
//   docs/images/*             figures referenced by the docs
//   CHANGELOG.md              changelog page
//   docs/Doxyfile.in, include/, src/lib/
//                             Doxygen reference in /docs/doxygen/ (needs doxygen)
//   site/src/**               landing page, styles, scripts, fonts, benchmark data
//
// Only dependency: `marked` (Markdown -> HTML). Highlighting, slugs, link
// rewriting and the sitemap are done here, with no other packages.

import { readFile, writeFile, mkdir, rm, cp, stat } from "node:fs/promises";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { createServer } from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { Marked } from "marked";

const SITE = path.dirname(fileURLToPath(import.meta.url));
// The zxc repository this site lives in (its parent folder by default). CI points
// REPO_ROOT at a checkout of the latest release tag, so the docs match the release.
const REPO = path.resolve(process.env.REPO_ROOT || path.join(SITE, ".."));
const SRC = path.join(SITE, "src");
const OUT = path.join(SITE, "dist");
const SITE_URL = (process.env.SITE_URL || "https://libzxc.org").replace(/\/$/, "");
const GH = "https://github.com/hellobertrand/zxc";
const BRANCH = process.env.SOURCE_BRANCH || "main";

// ---------------------------------------------------------------------------
// Documentation pages. Order here is the order in the sidebar and on /docs/.
// ---------------------------------------------------------------------------
const DOCS = [
  { group: "Guides", slug: "whitepaper", file: "docs/WHITEPAPER.md", title: "Whitepaper",
    blurb: "Why an asymmetric codec, how the bitstream is laid out, and where the speed comes from." },
  { group: "Guides", slug: "examples", file: "docs/EXAMPLES.md", title: "Examples",
    blurb: "Complete programs for the buffer, stream, context, seekable and dictionary APIs." },
  { group: "Guides", slug: "install", file: "docs/INSTALL.md", title: "Integration",
    blurb: "Vendoring ZXC, building as a subproject, and embedding it in another build." },
  { group: "Guides", slug: "migration", file: "docs/MIGRATION.md", title: "Migration",
    blurb: "Upgrading between releases and converting archives across format versions." },
  { group: "Reference", slug: "api", file: "docs/API.md", title: "API reference",
    blurb: "Every public function, type, constant and error code, with ABI rules." },
  { group: "Reference", slug: "cli", file: "docs/man/zxc.1.md", title: "CLI manual",
    blurb: "The zxc(1) manual page: modes, options, exit codes." },
  { group: "Reference", slug: "format", file: "docs/FORMAT.md", title: "Format specification",
    blurb: "The on-disk wire format, precise enough to write an independent decoder." },
  { group: "Project", slug: "changelog", file: "CHANGELOG.md", title: "Changelog",
    blurb: "What changed in each release." },
];
const DOC_BY_FILE = new Map(DOCS.map((d) => [d.file, d]));

// The Doxygen reference: generated HTML, listed on /docs/ but outside the page chain.
// It has no link back to the site, so it opens in a new tab.
const DOXYGEN = { group: "Reference", href: "/docs/doxygen/", title: "Doxygen reference", newTab: true,
  blurb: "Generated from the headers and sources: every symbol with its documentation and source, searchable." };

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
const esc = (s) => String(s)
  .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
const unesc = (s) => String(s)
  .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/&#39;/g, "'").replace(/&amp;/g, "&");
const stripTags = (s) => unesc(String(s).replace(/<[^>]*>/g, ""));
const fmt = (n) => (n >= 100 ? Math.round(n).toLocaleString("en-US") : String(n));

/** GitHub-compatible heading slug, so anchors written for GitHub keep working. */
function slugify(text) {
  return text.trim().toLowerCase()
    .replace(/[^\p{L}\p{M}\p{N}\p{Pc} -]/gu, "")
    .replace(/ /g, "-");
}

async function readRepo(rel) {
  return readFile(path.join(REPO, rel), "utf8");
}

// ---------------------------------------------------------------------------
// Syntax highlighting (tiny, dependency-free, good enough for docs snippets)
// ---------------------------------------------------------------------------
const C_KW = new Set(("auto break case char const continue default do double else enum extern float for goto if " +
  "inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void " +
  "volatile while bool true false NULL size_t ssize_t uint8_t uint16_t uint32_t uint64_t int8_t int16_t int32_t " +
  "int64_t uintptr_t FILE _Bool _Static_assert static_assert " +
  // rust / go / js / ts, for the bindings snippets
  "fn let mut use pub impl match mod crate self Self struct trait where as ref move loop in " +
  "func package import var type range defer go chan map interface nil " +
  "function const let await async new return export from require this undefined class extends").split(" "));
const PY_KW = new Set(("and as assert async await break class continue def del elif else except False finally for " +
  "from global if import in is lambda None nonlocal not or pass raise return True try while with yield").split(" "));

const LANGS = {
  clike: [
    [/\/\/[^\n]*/y, "com"],
    [/\/\*[\s\S]*?\*\//y, "com"],
    [/^[ \t]*#[ \t]*\w+[^\n]*/my, "pp"],
    [/"(?:\\.|[^"\\\n])*"/y, "str"],
    [/'(?:\\.|[^'\\\n])+'/y, "str"],
    [/\b(?:0x[0-9a-fA-F]+|\d+(?:\.\d+)?)(?:[uUlLfF]*)\b/y, "num"],
    [/[A-Za-z_]\w*(?=\s*\()/y, "fn"],
    [/[A-Za-z_]\w*/y, (w) => (C_KW.has(w) ? "kw" : null)],
  ],
  sh: [
    [/(?:^|(?<=\s))#[^\n]*/my, "com"],
    [/"(?:\\.|[^"\\])*"/y, "str"],
    [/'[^']*'/y, "str"],
    [/(?:^|(?<=[|;&]\s*)|(?<=\$\())[ \t]*(?:sudo[ \t]+)?[A-Za-z_][\w.-]*/my, "fn"],
    [/(?<=\s)--?[A-Za-z][\w-]*/y, "kw"],
  ],
  py: [
    [/#[^\n]*/y, "com"],
    [/"""[\s\S]*?"""|'''[\s\S]*?'''/y, "str"],
    [/[bf]?"(?:\\.|[^"\\\n])*"|[bf]?'(?:\\.|[^'\\\n])*'/y, "str"],
    [/\b\d+(?:\.\d+)?\b/y, "num"],
    [/[A-Za-z_]\w*(?=\s*\()/y, "fn"],
    [/[A-Za-z_]\w*/y, (w) => (PY_KW.has(w) ? "kw" : null)],
  ],
  cmake: [
    [/#[^\n]*/y, "com"],
    [/"(?:\\.|[^"\\])*"/y, "str"],
    [/[A-Za-z_]\w*(?=\s*\()/y, "fn"],
    [/\b[A-Z][A-Z0-9_]{2,}\b/y, "kw"],
  ],
  json: [
    [/"(?:\\.|[^"\\])*"(?=\s*:)/y, "fn"],
    [/"(?:\\.|[^"\\])*"/y, "str"],
    [/-?\b\d+(?:\.\d+)?\b/y, "num"],
    [/\b(?:true|false|null)\b/y, "kw"],
  ],
  ini: [
    [/[;#][^\n]*/y, "com"],
    [/^\[[^\]\n]*\]/my, "kw"],
    [/^[\w.-]+(?=\s*=)/my, "fn"],
  ],
};
const LANG_ALIAS = {
  c: "clike", h: "clike", cpp: "clike", "c++": "clike", rust: "clike", rs: "clike", go: "clike",
  js: "clike", javascript: "clike", ts: "clike", typescript: "clike", mjs: "clike",
  sh: "sh", bash: "sh", shell: "sh", console: "sh", zsh: "sh",
  python: "py", py: "py", cmake: "cmake", meson: "py", json: "json", ini: "ini", toml: "ini",
};

function highlight(code, lang) {
  const rules = LANGS[LANG_ALIAS[(lang || "").toLowerCase()]];
  if (!rules) return esc(code);
  let out = "", i = 0, plain = "";
  const flush = () => { if (plain) { out += esc(plain); plain = ""; } };
  outer: while (i < code.length) {
    for (const [re, cls] of rules) {
      re.lastIndex = i;
      const m = re.exec(code);
      if (m && m.index === i && m[0].length) {
        const c = typeof cls === "function" ? cls(m[0]) : cls;
        if (c) { flush(); out += `<span class="tok-${c}">${esc(m[0])}</span>`; }
        else plain += m[0];
        i += m[0].length;
        continue outer;
      }
    }
    plain += code[i++];
  }
  flush();
  return out;
}

/** Highlight every <pre class="code" data-lang=".."><code>..</code></pre> in hand-written HTML. */
function highlightHtml(html) {
  return html.replace(/<pre class="code" data-lang="([\w+-]+)"><code>([\s\S]*?)<\/code><\/pre>/g,
    (_, lang, body) => `<pre class="code" data-lang="${lang}"><code>${highlight(unesc(body), lang)}</code></pre>`);
}

// ---------------------------------------------------------------------------
// Markdown rendering with repo-aware links and GitHub-style anchors
// ---------------------------------------------------------------------------
function makeMarkdown(doc) {
  const seen = new Map();
  const toc = [];
  let title = null;
  const baseDir = path.posix.dirname(doc.file);

  function uniqueSlug(text) {
    const base = slugify(text);
    const n = seen.get(base) || 0;
    seen.set(base, n + 1);
    return n ? `${base}-${n}` : base;
  }

  function rewriteHref(href, { image = false } = {}) {
    if (!href) return href;
    if (/^(?:[a-z]+:|#|\/\/)/i.test(href)) {
      // Absolute links back into this repository's docs -> local pages.
      const m = href.match(/^https:\/\/github\.com\/hellobertrand\/zxc\/blob\/[^/]+\/([^#?]+)(#.*)?$/);
      if (m && DOC_BY_FILE.has(m[1])) return `/docs/${DOC_BY_FILE.get(m[1]).slug}/${m[2] || ""}`;
      return href;
    }
    const [rel, hash = ""] = href.split("#");
    const target = path.posix.normalize(path.posix.join(baseDir, decodeURI(rel)));
    const anchor = hash ? `#${hash}` : "";
    if (DOC_BY_FILE.has(target)) return `/docs/${DOC_BY_FILE.get(target).slug}/${anchor}`;
    if (target === "README.md") {
      // README sections that exist on the landing page; anything else stays on GitHub.
      const onLanding = { "": "", installation: "#download", benchmarks: "#benchmarks", "quick-start": "#start",
        "compression-levels": "#levels", "language-bindings": "#bindings", "safety--quality": "#quality" };
      return hash in onLanding ? `/${onLanding[hash]}` : `${GH}/blob/${BRANCH}/README.md${anchor}`;
    }
    if (target.startsWith("docs/images/")) return `/${target}`;
    if (target.startsWith("..")) return href;
    const kind = image ? "raw" : (path.posix.extname(target) ? "blob" : "tree");
    return `${GH}/${kind}/${BRANCH}/${target}${anchor}`;
  }

  const marked = new Marked({ gfm: true, breaks: doc.slug === "changelog" });
  marked.use({
    renderer: {
      heading({ tokens, depth }) {
        const inner = this.parser.parseInline(tokens);
        const text = stripTags(inner);
        if (depth === 1 && title === null) { title = text; return ""; }
        const id = uniqueSlug(text);
        if (depth === 2 || (depth === 3 && doc.slug !== "changelog" && doc.slug !== "api")) {
          toc.push({ depth, id, text });
        }
        const anchor = depth > 1 ? `<a class="anchor" href="#${id}" aria-label="Link to this section">#</a>` : "";
        return `<h${depth} id="${esc(id)}">${inner}${anchor}</h${depth}>\n`;
      },
      code({ text, lang }) {
        const l = (lang || "").split(/\s+/)[0];
        return `<pre class="code"${l ? ` data-lang="${esc(l)}"` : ""}><code>${highlight(text, l)}</code></pre>\n`;
      },
      link({ href, title: t, tokens }) {
        const inner = this.parser.parseInline(tokens);
        const h = rewriteHref(href);
        const ext = /^https?:/.test(h) ? ' rel="noopener"' : "";
        return `<a href="${esc(h)}"${t ? ` title="${esc(t)}"` : ""}${ext}>${inner}</a>`;
      },
      image({ href, title: t, text }) {
        return `<img src="${esc(rewriteHref(href, { image: true }))}" alt="${esc(text)}"${t ? ` title="${esc(t)}"` : ""} loading="lazy">`;
      },
      table(token) {
        // Let marked build the table, then wrap it so wide tables scroll on phones.
        const html = marked.Renderer.prototype.table.call(this, token);
        return `<div class="table-scroll">${html}</div>\n`;
      },
    },
  });

  return {
    render(md) {
      // The sidebar already lists every section: drop hand-written TOCs.
      let src = md.replace(/^#{2,3}\s+(?:Table of Contents|Contents)\s*\n[\s\S]*?(?=^#{1,6}\s|^---\s*$)/im, "");
      if (doc.slug === "changelog") {
        // Link PR / issue references to GitHub.
        src = src.replace(/\(#(\d+)\)/g, `([#$1](${GH}/pull/$1))`);
      }
      const html = marked.parse(src);
      return { html, toc, title };
    },
  };
}

// Content hashes for cache busting, so CSS/JS can be cached as immutable.
const ASSET_V = { css: "", js: "" };
// Filled in by build(): shown in the footer.
const META = { version: "", releaseDate: "" };

// ---------------------------------------------------------------------------
// Page chrome (GNU-style masthead, navigation bar and footer)
// ---------------------------------------------------------------------------
const NAV = [
  ["/", "About"],
  ["/docs/", "Documentation"],
  ["/#download", "Download"],
  ["/#benchmarks", "Benchmarks"],
  ["/docs/changelog/", "Changelog"],
  [GH, "Source code"],
  ["/#help", "Help"],
];

function header(active) {
  const links = NAV.map(([href, label]) =>
    `<li><a href="${href}"${active === href ? ' aria-current="page"' : ""}>${label}</a></li>`).join("");
  return `<a class="skip" href="#main">Skip to content</a>
<header class="masthead">
  <div class="inner">
    <div>
      <p class="site-name"><a href="/">ZXC</a></p>
      <p class="site-tag">Asymmetric lossless compression, built for fast decoding</p>
    </div>
  </div>
  <nav class="navbar" aria-label="Main"><ul>${links}</ul></nav>
</header>`;
}

function footer() {
  const year = new Date().getUTCFullYear();
  const today = new Date().toISOString().slice(0, 10);
  return `<footer class="footer">
  <div class="inner">
    <p><a href="#main">Back to top</a></p>
    <p>Copyright &copy; 2025&ndash;${year} Bertrand Lebonnois and contributors.</p>
    <p>ZXC is free software, released under the BSD 3-Clause License. This site is generated from the <a href="${GH}">zxc repository</a>; please report problems with it on the <a href="${GH}/issues">issue tracker</a>.</p>
    <p>Updated: ${today} (ZXC ${esc(META.version)}).</p>
  </div>
</footer>`;
}



function docBody(doc, { html, toc, title }, version) {
  const t = esc(title || doc.title);
  const meta = `<p class="doc-meta">ZXC ${esc(version)}. Generated from <a href="${GH}/blob/${BRANCH}/${doc.file}"><code>${esc(doc.file)}</code></a>; corrections are welcome as <a href="${GH}/edit/main/${doc.file}">pull requests</a>.</p>`;
  const contents = doc.slug === "changelog" ? "" : contentsBlock(toc);
  return `<div class="inner prose">
${nodeNav(doc)}
<hr>
<h1>${t}</h1>
${meta}
${contents}
${html}
<hr>
${nodeNav(doc)}
</div>`;
}

function layout({ title, description, urlPath, body, active, jsonLd }) {
  const canonical = SITE_URL + urlPath;
  const fullTitle = urlPath === "/" ? title : `${title} - ZXC`;
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>${esc(fullTitle)}</title>
<meta name="description" content="${esc(description)}">
<link rel="canonical" href="${canonical}">
<meta property="og:type" content="website">
<meta property="og:site_name" content="ZXC">
<meta property="og:title" content="${esc(fullTitle)}">
<meta property="og:description" content="${esc(description)}">
<meta property="og:url" content="${canonical}">
<meta property="og:image" content="${SITE_URL}/assets/og.png">
<meta property="og:image:width" content="1200">
<meta property="og:image:height" content="630">
<meta name="twitter:card" content="summary_large_image">
<link rel="icon" href="data:,">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Noto+Sans:ital,wght@0,400;0,700;1,400&family=Noto+Sans+Mono:wght@400;700&display=swap">
<link rel="stylesheet" href="/assets/site.css?v=${ASSET_V.css}">
<script src="/assets/site.js?v=${ASSET_V.js}" defer></script>
${jsonLd ? `<script type="application/ld+json">${JSON.stringify(jsonLd)}</script>` : ""}
</head>
<body>
${header(active)}
<main id="main">
${body}
</main>
${footer()}
</body>
</html>
`;
}

// ---------------------------------------------------------------------------
// Landing page pieces generated from benchmarks.json
// ---------------------------------------------------------------------------
function benchLookup(cpu) {
  const m = new Map();
  for (const [name, comp, dec, ratio] of cpu.rows) m.set(name, { name, comp, dec, ratio });
  return m;
}

function gainHtml(z, r) {
  const x = z.dec / r.dec;
  let speed;
  if (x >= 1.02) speed = `<b>${x.toFixed(2)}&times; faster</b> decode`;
  else if (x > 0.98) speed = `<b>Decode within ${Math.max(1, Math.round(Math.abs(1 - x) * 100))}&#8239;%</b>`;
  else speed = `<b>${(1 / x).toFixed(2)}&times; slower</b> decode`;
  const size = z.ratio <= r.ratio ? "smaller output" : "larger output";
  return `${speed}, ${size} (${z.ratio.toFixed(2)}&#8239;% vs ${r.ratio.toFixed(2)}&#8239;%)`;
}

function renderFigure(bench) {
  const cpus = bench.cpus;
  const first = cpus[0];
  const perCpu = Object.fromEntries(cpus.map((c) => {
    const L = benchLookup(c);
    const max = Math.max(...bench.tiers.flatMap((t) => [L.get(t.zxc).dec, L.get(t.rival).dec]));
    return [c.id, { L, max }];
  }));

  const bar = (tier, which) => {
    const name = tier[which];
    const byCpu = {}, byCpuVal = {};
    for (const c of cpus) {
      const { L, max } = perCpu[c.id];
      const row = L.get(name);
      byCpu[c.id] = { w: +(row.dec / max).toFixed(4) };
      byCpuVal[c.id] = { html: `${fmt(row.dec)} MB/s` };
    }
    const f = byCpu[first.id], v = byCpuVal[first.id];
    return `<div class="bar ${which === "zxc" ? "zxc" : "rival"}">
          <div class="bar-label"><span class="codec">${esc(name)}</span><span class="val" data-by-cpu='${JSON.stringify(byCpuVal)}'>${v.html}</span></div>
          <div class="track"><div class="fill" style="--w:${f.w}" data-by-cpu='${JSON.stringify(byCpu)}'></div></div>
        </div>`;
  };

  const tiers = bench.tiers.map((t) => {
    const gains = Object.fromEntries(cpus.map((c) => {
      const { L } = perCpu[c.id];
      return [c.id, { html: gainHtml(L.get(t.zxc), L.get(t.rival)) }];
    }));
    const r = perCpu[first.id].L.get(t.zxc).ratio;
    return `<div class="tier">
      <div class="tier-name">${esc(t.name)}<small>${r.toFixed(2)}&#8239;% of original</small></div>
      <div class="bars">
        ${bar(t, "zxc")}
        ${bar(t, "rival")}
        <div class="gain" data-by-cpu='${JSON.stringify(gains).replace(/'/g, "&#39;")}'>${gains[first.id].html}</div>
      </div>
    </div>`;
  }).join("\n");

  const radios = cpus.map((c, i) => `<label><input type="radio" name="cpu" value="${c.id}"${i === 0 ? " checked" : ""}> ${esc(c.label)}</label>`).join("\n        ");
  const details = Object.fromEntries(cpus.map((c) => [c.id, c.detail]));

  return `<figure class="bench" data-bench-figure>
    <p class="bench-title">Decompression speed against codecs of similar size</p>
    <fieldset>
      <legend>Processor:</legend>
        ${radios}
    </fieldset>
    ${tiers}
    <figcaption>Figure 1. Single-threaded decompression of <code>${esc(bench.corpus)}</code> (${(bench.corpusBytes / 1e6).toFixed(1)}&nbsp;MB), measured with ${esc(bench.tool)} and zxc ${esc(bench.zxcVersion)}. ZXC levels 1, 3, 6 and 7 are each compared with the codec that produces output of similar size. <span data-cpu-detail='${JSON.stringify(details).replace(/'/g, "&#39;")}'>${esc(first.detail)}</span>. Full results are in the <a href="#benchmarks">benchmark tables</a>.</figcaption>
  </figure>`;
}

function renderBenchTables(bench) {
  return bench.cpus.map((c, i) => {
    const rows = c.rows.map(([name, comp, dec, ratio]) => {
      const z = name.startsWith("zxc");
      return `<tr${z ? ' class="is-zxc"' : ""}><td class="codec">${esc(name)}</td><td class="num">${fmt(comp)}</td><td class="num">${fmt(dec)}</td><td class="num">${ratio.toFixed(2)}</td></tr>`;
    }).join("\n");
    return `<details${i === 0 ? " open" : ""}>
  <summary><strong>${esc(c.label)}</strong> (${esc(c.arch)})</summary>
  <p class="note">${esc(c.detail)}. Single thread, ${esc(bench.tool)}, <code>${esc(bench.corpus)}</code>. Speeds in MB/s; size as a percentage of the input.</p>
  <div class="table-scroll"><table>
    <thead><tr><th scope="col">Codec and level</th><th scope="col" class="num">Compression</th><th scope="col" class="num">Decompression</th><th scope="col" class="num">Size %</th></tr></thead>
    <tbody>${rows}</tbody>
  </table></div>
</details>`;
  }).join("\n");
}

const LEVEL_INFO = [
  [1, "Fastest", "Real-time assets, games, UI"],
  [2, "Fast", "Real-time assets, games, UI"],
  [3, "Default", "General use; smaller and faster to decode than LZ4"],
  [4, "Balanced", "General use, a little denser"],
  [5, "Compact", "Embedded and firmware"],
  [6, "Density", "Archives and read-mostly data; beats LZ4HC on both axes"],
  [7, "Ultra", "When size dominates; smaller than zstd -1, decodes about 2&times; faster"],
];

function renderLevels(bench) {
  const L = benchLookup(bench.cpus[0]);
  const rows = LEVEL_INFO.map(([n, name, use]) => {
    const r = L.get(`zxc -${n}`);
    return `<tr${n === 3 ? ' class="is-zxc"' : ""}><td class="num">${n}</td><td>${name}</td><td>${use}</td><td class="num">${r ? r.ratio.toFixed(2) : ""}</td><td class="num">${r ? fmt(r.dec) : ""}</td><td class="num">${r ? fmt(r.comp) : ""}</td></tr>`;
  }).join("\n");
  return `<div class="table-scroll"><table>
    <thead><tr><th scope="col">Level</th><th scope="col">Name</th><th scope="col">Suited for</th><th scope="col" class="num">Size %</th><th scope="col" class="num">Decode MB/s</th><th scope="col" class="num">Encode MB/s</th></tr></thead>
    <tbody>${rows}</tbody>
  </table></div>`;
}

// ---------------------------------------------------------------------------
// Docs chrome
// ---------------------------------------------------------------------------
function nodeNav(doc) {
  const i = DOCS.indexOf(doc);
  const prev = DOCS[i - 1], next = DOCS[i + 1];
  const parts = [];
  if (next) parts.push(`Next: <a href="/docs/${next.slug}/" rel="next">${esc(next.title)}</a>`);
  if (prev) parts.push(`Previous: <a href="/docs/${prev.slug}/" rel="prev">${esc(prev.title)}</a>`);
  parts.push(`Up: <a href="/docs/" rel="up">Documentation</a>`);
  return `<div class="nodenav">${parts.join(", ")}</div>`;
}

function contentsBlock(toc) {
  if (toc.length < 3) return "";
  const items = [];
  for (const t of toc) {
    if (t.depth === 2) items.push({ ...t, kids: [] });
    else if (items.length) items[items.length - 1].kids.push(t);
  }
  const li = (t) => `<li><a href="#${esc(t.id)}">${esc(t.text)}</a>${t.kids && t.kids.length ? `<ul>${t.kids.map(li).join("")}</ul>` : ""}</li>`;
  return `<div class="contents" id="contents"><h2>Table of Contents</h2><ul>${items.map(li).join("")}</ul></div>`;
}

// ---------------------------------------------------------------------------
// Doxygen reference
// ---------------------------------------------------------------------------
/** Runs Doxygen on docs/Doxyfile.in into OUT/docs/doxygen. Returns false if skipped. */
async function buildDoxygen(version) {
  const tpl = path.join(REPO, "docs/Doxyfile.in");
  if (!existsSync(tpl)) { console.warn("skip: docs/Doxyfile.in not found"); return false; }
  // The @VAR@ CMake would substitute, then the overrides for the site (last assignment wins).
  const conf = (await readFile(tpl, "utf8"))
    .replaceAll("@PROJECT_NAME@", "ZXC")
    .replaceAll("@PROJECT_VERSION@", version)
    .replaceAll("@PROJECT_DESCRIPTION@", "High-performance asymmetric lossless compression library") +
    `\nOUTPUT_DIRECTORY = "${path.join(OUT, "docs")}"\nHTML_OUTPUT = doxygen\nQUIET = YES\nWARNINGS = NO\nWARN_IF_INCOMPLETE_DOC = NO\nWARN_NO_PARAMDOC = NO\n`;
  const bin = process.env.DOXYGEN || "doxygen";
  const r = spawnSync(bin, ["-"], { cwd: REPO, input: conf, stdio: ["pipe", "inherit", "inherit"] });
  if (r.error) {
    // CI must publish the reference; a local preview can do without it.
    if (process.env.CI) throw new Error(`${bin} not found: ${r.error.message}`);
    console.warn(`skip: ${bin} not found, no Doxygen reference`);
    return false;
  }
  if (r.status !== 0) throw new Error(`${bin} exited with status ${r.status}`);
  return true;
}

// ---------------------------------------------------------------------------
// Build
// ---------------------------------------------------------------------------
async function build() {
  const t0 = Date.now();
  if (!existsSync(path.join(REPO, "include/zxc_constants.h"))) {
    throw new Error(`No zxc checkout at ${REPO}. Set REPO_ROOT to a checkout of hellobertrand/zxc.`);
  }
  await rm(OUT, { recursive: true, force: true });
  await mkdir(OUT, { recursive: true });

  const constants = await readRepo("include/zxc_constants.h");
  const v = (k) => (constants.match(new RegExp(`#define\\s+ZXC_VERSION_${k}\\s+(\\d+)`)) || [])[1];
  const version = [v("MAJOR"), v("MINOR"), v("PATCH")].join(".");
  if (!/^\d+\.\d+\.\d+$/.test(version)) throw new Error("Could not read version from include/zxc_constants.h");
  const formatVersion = ((await readRepo("docs/FORMAT.md")).match(/\*\*Format Version\*\*:\s*(\d+)/) || [])[1] || "";
  const bench = JSON.parse(await readFile(path.join(SRC, "data/benchmarks.json"), "utf8"));
  const changelog = existsSync(path.join(REPO, "CHANGELOG.md")) ? await readRepo("CHANGELOG.md") : "";
  const releaseDate = (changelog.match(new RegExp(`^## \\[${version.replace(/\./g, "\\.")}\\] - (\\d{4}-\\d{2}-\\d{2})`, "m")) || [])[1] || "";
  META.version = version;
  META.releaseDate = releaseDate;

  const hash8 = async (f) => createHash("sha256").update(await readFile(path.join(SRC, f))).digest("hex").slice(0, 8);
  ASSET_V.css = await hash8("assets/site.css");
  ASSET_V.js = await hash8("assets/site.js");

  const pages = [];
  const write = async (urlPath, html) => {
    const file = urlPath.endsWith("/") ? path.join(OUT, urlPath, "index.html") : path.join(OUT, urlPath);
    await mkdir(path.dirname(file), { recursive: true });
    await writeFile(file, html);
    if (urlPath.endsWith("/")) pages.push(urlPath);
  };

  // Static assets
  await cp(path.join(SRC, "assets"), path.join(OUT, "assets"), { recursive: true });
  if (existsSync(path.join(REPO, "docs/images"))) {
    await cp(path.join(REPO, "docs/images"), path.join(OUT, "docs/images"), { recursive: true });
  }

  // Landing page
  const landing = (await readFile(path.join(SRC, "index.html"), "utf8"))
    .replaceAll("{{VERSION}}", esc(version))
    .replaceAll("{{FORMAT_VERSION}}", esc(formatVersion))
    .replaceAll("{{BENCH_TOOL}}", esc(bench.tool))
    .replaceAll("{{RELEASE_DATE}}", releaseDate ? `, released on ${releaseDate}` : "")
    .replace("{{BENCH_FIGURE}}", () => renderFigure(bench))
    .replace("{{BENCH_TABLES}}", () => renderBenchTables(bench))
    .replace("{{LEVELS_TABLE}}", () => renderLevels(bench));
  await write("/", layout({
    title: "ZXC: lossless compression built for fast decode",
    description: "ZXC is a lossless compression C library for write-once, read-many data. It decodes faster than LZ4 at a smaller size, with seekable archives, dictionaries and SIMD on ARM and x86.",
    urlPath: "/",
    body: highlightHtml(landing),
    active: "/",
    jsonLd: {
      "@context": "https://schema.org",
      "@type": "SoftwareSourceCode",
      name: "ZXC",
      alternateName: "libzxc",
      description: "Asymmetric lossless compression library built for ultra-fast decode.",
      codeRepository: GH,
      programmingLanguage: "C",
      license: "https://opensource.org/license/bsd-3-clause",
      version,
      author: { "@type": "Person", name: "Bertrand Lebonnois" },
      url: SITE_URL + "/",
    },
  }));

  // Documentation pages
  for (const doc of DOCS) {
    const abs = path.join(REPO, doc.file);
    if (!existsSync(abs)) { console.warn(`skip: ${doc.file} not found`); continue; }
    const { html, toc, title } = makeMarkdown(doc).render(await readFile(abs, "utf8"));
    const body = docBody(doc, { html, toc, title }, version);
    await write(`/docs/${doc.slug}/`, layout({
      title: doc.title,
      description: doc.blurb,
      urlPath: `/docs/${doc.slug}/`,
      body,
      active: "/docs/",
    }));
  }

  // Doxygen reference
  const entries = DOCS.map((d) => ({ ...d, href: `/docs/${d.slug}/` }));
  if (await buildDoxygen(version)) {
    entries.push(DOXYGEN);
    pages.push(DOXYGEN.href);
  }

  // Docs index, grouped like a GNU manual directory
  const groups = [...new Set(entries.map((d) => d.group))];
  const lists = groups.map((g) => `<h2>${esc(g)}</h2>
<dl class="doclist">
${entries.filter((d) => d.group === g).map((d) => `  <dt><a href="${d.href}"${d.newTab ? ' target="_blank" rel="noopener"' : ""}>${esc(d.title)}</a></dt>\n  <dd>${esc(d.blurb)}</dd>`).join("\n")}
</dl>`).join("\n");
  await write("/docs/", layout({
    title: "Documentation",
    description: "ZXC documentation: whitepaper, examples, API reference, CLI manual and wire format specification.",
    urlPath: "/docs/",
    active: "/docs/",
    body: `<div class="inner">
<h1>ZXC documentation</h1>
<p>This documentation describes ZXC ${esc(version)}. It is generated from the <code>docs/</code> directory of the source repository, so it always matches the released code. If you are new to ZXC, start with the <a href="/#start">quick start</a> and then the <a href="/docs/examples/">examples</a>.</p>
${lists}
</div>`,
  }));

  // 404
  const nf = layout({
    title: "Page not found",
    description: "This page does not exist.",
    urlPath: "/404.html",
    active: "",
    body: `<div class="inner"><h1>Page not found</h1><p>There is no page at this address. It may be mistyped, or the page may have moved.</p><p>Try the <a href="/">home page</a> or the <a href="/docs/">documentation</a>.</p></div>`,
  });
  await writeFile(path.join(OUT, "404.html"), nf.replace('<meta name="description"', '<meta name="robots" content="noindex">\n<meta name="description"'));

  // SEO + hosting files
  const today = new Date().toISOString().slice(0, 10);
  await writeFile(path.join(OUT, "sitemap.xml"),
    `<?xml version="1.0" encoding="UTF-8"?>\n<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n` +
    pages.map((p) => `  <url><loc>${SITE_URL}${p}</loc><lastmod>${today}</lastmod></url>`).join("\n") +
    `\n</urlset>\n`);
  await writeFile(path.join(OUT, "robots.txt"), `User-agent: *\nAllow: /\n\nSitemap: ${SITE_URL}/sitemap.xml\n`);

  console.log(`built ${pages.length} pages for ZXC ${version} (format v${formatVersion}) in ${Date.now() - t0} ms -> ${path.relative(process.cwd(), OUT) || "."}`);
}

// ---------------------------------------------------------------------------
// Local preview server (no dependency)
// ---------------------------------------------------------------------------
const TYPES = { ".html": "text/html; charset=utf-8", ".css": "text/css", ".js": "text/javascript", ".svg": "image/svg+xml",
  ".png": "image/png", ".woff2": "font/woff2", ".json": "application/json", ".xml": "application/xml", ".txt": "text/plain" };

function serve(port = Number(process.env.PORT) || 8080) {
  createServer(async (req, res) => {
    try {
      let p = decodeURIComponent(new URL(req.url, "http://x").pathname);
      let file = path.join(OUT, p);
      if (!file.startsWith(OUT)) throw new Error("bad path");
      if (existsSync(file) && (await stat(file)).isDirectory()) file = path.join(file, "index.html");
      if (!existsSync(file)) { res.writeHead(404, { "content-type": TYPES[".html"] }); res.end(await readFile(path.join(OUT, "404.html"))); return; }
      res.writeHead(200, { "content-type": TYPES[path.extname(file)] || "application/octet-stream" });
      res.end(await readFile(file));
    } catch { res.writeHead(400); res.end(); }
  }).listen(port, () => console.log(`preview on http://localhost:${port}`));
}

await build();
if (process.argv.includes("--serve")) serve();
