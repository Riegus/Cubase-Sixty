#!/usr/bin/env python3
"""Makes cubase/docs/user-guide.html from cubase/docs/user-guide.md.

    pip install markdown
    python cubase/tools/make-user-guide.py

The HTML is one file with no outside resources: it is installed beside the tray app and opened from its menu.
Run this after every change to user-guide.md and commit both.
"""
import pathlib
import re

import markdown

ROOT = pathlib.Path(__file__).resolve().parents[1]
SRC = ROOT / "docs" / "user-guide.md"
OUT = ROOT / "docs" / "user-guide.html"
GITHUB = "https://github.com/Riegus/Cubase-Sixty/blob/cubase/cubase/docs/"

# The Settings page's colours and layout (bridge/src/settings-page.html): keep the two alike.
CSS = """
:root {
  --bg: #f4f4f2; --panel: #ffffff; --ink: #1d1d1b; --muted: #6b6b66; --line: #dcdcd6;
  --accent: #2f7d4f; --accent-ink: #ffffff; --rail: #ebebe6; --focus: #2f7d4f55;
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg: #161615; --panel: #1f1f1d; --ink: #ecece6; --muted: #9a9a92; --line: #33332f;
    --accent: #5cc285; --accent-ink: #0d1f14; --rail: #1a1a18; --focus: #5cc28566;
  }
}
* { box-sizing: border-box; }
html { scroll-padding-top: 16px; }
body { margin: 0; background: var(--bg); color: var(--ink); font: 15px/1.55 "Segoe UI", system-ui, sans-serif; }
.app { display: grid; grid-template-columns: 240px 1fr; min-height: 100vh; }
nav { background: var(--rail); border-right: 1px solid var(--line); padding: 20px 12px; position: sticky; top: 0;
      height: 100vh; overflow-y: auto; }
.brand { display: flex; align-items: center; gap: 10px; padding: 0 8px 2px; font-weight: 600; letter-spacing: .2px; }
.brand svg { flex: none; }
.ver { color: var(--muted); font-size: 12px; padding: 0 8px 16px 40px; }
nav ul { list-style: none; margin: 0; padding: 0; }
nav li { margin: 0; }
nav a { display: block; padding: 7px 10px; border-radius: 6px; color: var(--ink); text-decoration: none; }
nav a:hover { background: var(--line); }
nav a.on { background: var(--accent); color: var(--accent-ink); }
nav > .toc > ul > li > a { display: none; }
nav ul ul ul { display: none; margin: 2px 0 6px; }
nav li.open > ul { display: block; }
nav ul ul ul a { padding: 4px 10px 4px 22px; font-size: 13px; color: var(--muted); }
nav ul ul ul a.on { background: var(--line); color: var(--ink); }
main { padding: 28px 36px 80px; max-width: 900px; min-width: 0; }
h1 { font-size: 22px; margin: 0 0 4px; }
h1 + p { color: var(--muted); }
.card { background: var(--panel); border: 1px solid var(--line); border-radius: 10px; padding: 18px 22px; margin: 16px 0; }
.card > :last-child { margin-bottom: 0; }
h2 { font-size: 18px; margin: 0 0 8px; }
h3 { font-size: 15px; margin: 22px 0 6px; padding-top: 14px; border-top: 1px solid var(--line); }
p { margin: 0 0 12px; }
ul, ol { margin: 0 0 12px; padding-left: 24px; }
li { margin: 4px 0; }
a { color: var(--accent); }
main a:focus-visible, nav a:focus-visible { outline: 3px solid var(--focus); outline-offset: 1px; }
hr { display: none; }
code { background: var(--bg); border: 1px solid var(--line); border-radius: 4px; padding: 0 5px; font: 13px Consolas, monospace; }
.tbl { overflow-x: auto; margin: 0 0 12px; border: 1px solid var(--line); border-radius: 8px; }
table { border-collapse: collapse; width: 100%; font-size: 14px; }
th, td { border-bottom: 1px solid var(--line); padding: 7px 12px; text-align: left; vertical-align: top; }
tr:last-child td { border-bottom: 0; }
th { background: var(--bg); color: var(--muted); font-weight: 600; font-size: 13px; }
td:first-child { white-space: nowrap; font-weight: 600; }
.search { padding: 0 2px 12px; }
.search input { width: 100%; padding: 7px 10px; border: 1px solid var(--line); border-radius: 6px; background: var(--panel);
                color: var(--ink); font: inherit; font-size: 14px; }
.search input:focus-visible { outline: 3px solid var(--focus); outline-offset: 1px; }
#results { display: none; }
nav.searching .toc { display: none; }
nav.searching #results { display: block; }
#results a { padding: 7px 10px; }
#results .where { font-size: 12px; color: var(--muted); display: block; }
#results .snip { font-size: 13px; color: var(--muted); display: block; margin-top: 2px; }
#results .none { color: var(--muted); font-size: 13px; padding: 4px 10px; }
mark { background: #f5d76e; color: #1d1d1b; border-radius: 2px; padding: 0 1px; }
mark.cur { background: #f0a030; }
@media (max-width: 760px) {
  .app { grid-template-columns: 1fr; } nav { display: none; } main { padding: 20px 16px 60px; }
  td:first-child { white-space: normal; }
}
@media print { nav { display: none; } .app { display: block; } .card { border: 0; padding: 0; } }
"""

BRAND = """<svg width="22" height="22" viewBox="0 0 16 16" aria-hidden="true"><rect x="0.5" y="1" width="15" height="14" rx="2.5" fill="var(--accent)"/><g fill="#1e1e1e"><rect x="3.9" y="3.5" width="1.2" height="9" rx=".6"/><rect x="7.4" y="3.5" width="1.2" height="9" rx=".6"/><rect x="10.9" y="3.5" width="1.2" height="9" rx=".6"/></g><g fill="#fff"><rect x="2.9" y="8.4" width="3.2" height="2.2" rx=".5"/><rect x="6.4" y="3.9" width="3.2" height="2.2" rx=".5"/><rect x="9.9" y="6.4" width="3.2" height="2.2" rx=".5"/></g></svg>"""

# The tab's icon: the brand mark (the Settings page's mixing surface) in the green of a running Cubase-Sixty.
FAVICON = ("data:image/svg+xml," + __import__("urllib.parse").parse.quote(
    '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><rect x="0.5" y="1" width="15" height="14" rx="2.5" fill="#2f7d4f"/>'
    '<g fill="#1e1e1e"><rect x="3.9" y="3.5" width="1.2" height="9" rx=".6"/><rect x="7.4" y="3.5" width="1.2" height="9" rx=".6"/>'
    '<rect x="10.9" y="3.5" width="1.2" height="9" rx=".6"/></g><g fill="#fff"><rect x="2.9" y="8.4" width="3.2" height="2.2" rx=".5"/>'
    '<rect x="6.4" y="3.9" width="3.2" height="2.2" rx=".5"/><rect x="9.9" y="6.4" width="3.2" height="2.2" rx=".5"/></g></svg>'))

# Marks the section being read in the rail, and opens its sub list. Then the search.
SCRIPT = """
(function () {
  var links = {}, heads = [].slice.call(document.querySelectorAll("main h2, main h3"));
  [].forEach.call(document.querySelectorAll("nav a"), function (a) { links[decodeURIComponent(a.getAttribute("href").slice(1))] = a; });
  function mark() {
    var cur = null, sec = null;
    heads.forEach(function (h) { if (h.getBoundingClientRect().top < 90) { cur = h; if (h.tagName === "H2") sec = h; } });
    if (!cur) { cur = sec = heads[0]; }
    [].forEach.call(document.querySelectorAll("nav a.on"), function (a) { a.classList.remove("on"); });
    [].forEach.call(document.querySelectorAll("nav li.open"), function (l) { l.classList.remove("open"); });
    if (sec && links[sec.id]) { links[sec.id].classList.add("on"); links[sec.id].parentNode.classList.add("open"); }
    if (cur !== sec && links[cur.id]) links[cur.id].classList.add("on");
  }
  addEventListener("scroll", mark, { passive: true }); mark();
})();
// Search: the matching sections in the rail (title matches first), the words marked in the text. Enter goes to the
// next mark, Escape clears; "/" puts the cursor in the box.
(function () {
  var nav = document.querySelector("nav"), box = document.getElementById("q"), list = document.getElementById("results");
  var main = document.querySelector("main"), sections = [], cur = null;
  var walk = document.createTreeWalker(main, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT);
  for (var n = walk.nextNode(); n; n = walk.nextNode()) {
    if (n.nodeType === 1 && (n.tagName === "H1" || n.tagName === "H2" || n.tagName === "H3")) {
      var chap = n.tagName === "H3" && cur ? (cur.tagName === "H2" ? cur.title : cur.chap) : "";
      cur = { el: n, tagName: n.tagName, title: n.textContent, chap: chap, text: "" };
      sections.push(cur);
    } else if (n.nodeType === 3 && cur && !(n.parentNode.closest && n.parentNode.closest("h1, h2, h3"))) cur.text += n.nodeValue;
  }
  function esc(s) { return s.replace(/[&<>]/g, function (c) { return { "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]; }); }
  function clearMarks() {
    [].slice.call(main.querySelectorAll("mark")).forEach(function (m) { var p = m.parentNode; p.replaceChild(document.createTextNode(m.textContent), m); p.normalize(); });
  }
  function markAll(q) {
    var lq = q.toLowerCase(), nodes = [], w = document.createTreeWalker(main, NodeFilter.SHOW_TEXT);
    for (var t = w.nextNode(); t; t = w.nextNode()) if (t.nodeValue.toLowerCase().indexOf(lq) >= 0) nodes.push(t);
    nodes.forEach(function (t) {
      var s = t.nodeValue, ls = s.toLowerCase(), frag = document.createDocumentFragment(), at = 0, i;
      while ((i = ls.indexOf(lq, at)) >= 0) {
        frag.appendChild(document.createTextNode(s.slice(at, i)));
        var m = document.createElement("mark"); m.textContent = s.slice(i, i + q.length); frag.appendChild(m);
        at = i + q.length;
      }
      frag.appendChild(document.createTextNode(s.slice(at)));
      t.parentNode.replaceChild(frag, t);
    });
  }
  var marks = [], at = -1;
  function go(i) {
    if (!marks.length) return;
    if (at >= 0 && marks[at]) marks[at].classList.remove("cur");
    at = (i + marks.length) % marks.length;
    marks[at].classList.add("cur");
    marks[at].scrollIntoView({ block: "center" });
  }
  function run() {
    var q = box.value.trim();
    clearMarks(); marks = []; at = -1; list.innerHTML = "";
    if (q.length < 2) { nav.classList.remove("searching"); return; }
    nav.classList.add("searching");
    var lq = q.toLowerCase(), hits = [];
    sections.forEach(function (s) {
      var inTitle = s.title.toLowerCase().indexOf(lq) >= 0, i = s.text.toLowerCase().indexOf(lq);
      if (!inTitle && i < 0) return;
      var count = 0, j = -1; while ((j = s.text.toLowerCase().indexOf(lq, j + 1)) >= 0) ++count;
      var snip = "";
      if (i >= 0) {
        var a = Math.max(0, i - 40), b = Math.min(s.text.length, i + q.length + 60);
        snip = (a ? "…" : "") + esc(s.text.slice(a, i)) + "<mark>" + esc(s.text.slice(i, i + q.length)) + "</mark>" + esc(s.text.slice(i + q.length, b)) + (b < s.text.length ? "…" : "");
      }
      hits.push({ s: s, score: (inTitle ? 1000 : 0) + count, snip: snip });
    });
    hits.sort(function (x, y) { return y.score - x.score; });
    if (!hits.length) list.innerHTML = '<div class="none">Nothing found for “' + esc(q) + '”.</div>';
    hits.slice(0, 40).forEach(function (h) {
      var a = document.createElement("a");
      a.href = "#" + (h.s.el.id || "");
      a.innerHTML = (h.s.chap ? '<span class="where">' + esc(h.s.chap) + "</span>" : "") + esc(h.s.title) + (h.snip ? '<span class="snip">' + h.snip + "</span>" : "");
      a.addEventListener("click", function () {
        var first = [].slice.call(main.querySelectorAll("mark")).filter(function (m) { return h.s.el.compareDocumentPosition(m) & 4; })[0];
        if (first) setTimeout(function () { go(marks.indexOf(first)); });
      });
      list.appendChild(a);
    });
    markAll(q);
    marks = [].slice.call(main.querySelectorAll("mark"));
  }
  var timer = 0;
  box.addEventListener("input", function () { clearTimeout(timer); timer = setTimeout(run, 150); });
  box.addEventListener("keydown", function (e) {
    if (e.key === "Enter") { e.preventDefault(); if (!marks.length) run(); go(e.shiftKey ? at - 1 : at + 1); }
    if (e.key === "Escape") { box.value = ""; run(); }
  });
  addEventListener("keydown", function (e) {
    if (e.key === "/" && document.activeElement !== box) { e.preventDefault(); box.focus(); box.select(); }
  });
})();
"""


def main():
    text = SRC.read_text(encoding="utf-8")
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    # The hand-made contents list at the top is for GitHub; the HTML has the side bar.
    text = re.sub(r"\*\*Getting started\*\*.*?\n---\n", "", text, count=1, flags=re.S)
    md = markdown.Markdown(extensions=["tables", "toc", "sane_lists"],
                           extension_configs={"toc": {"toc_depth": "1-3"}})
    body = md.convert(text)
    # Links to the other docs go to GitHub (they are not installed).
    body = re.sub(r'href="(?!https?:|#)([\w\-]+\.md)', lambda m: 'href="' + GITHUB + m.group(1), body)
    # Each chapter (h2 and what follows) in a card, as the Settings page's items.
    parts = re.split(r"(?=<h2 )", body)
    body = parts[0] + "".join('<div class="card">' + p + "</div>" for p in parts[1:])
    body = body.replace("<table>", '<div class="tbl"><table>').replace("</table>", "</table></div>")
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cubase-Sixty user guide</title>
<link rel="icon" type="image/svg+xml" href="{FAVICON}">
<style>{CSS}</style>
</head>
<body>
<div class="app">
<nav><div class="brand">{BRAND} Cubase-Sixty</div><div class="ver">User guide · {version}</div>
<div class="search"><input id="q" type="search" placeholder="Search the guide  ( / )" aria-label="Search the guide" autocomplete="off"></div>
<div id="results"></div>{md.toc}</nav>
<main>
{body}
</main>
</div>
<script>{SCRIPT}</script>
</body>
</html>
"""
    OUT.write_text(html, encoding="utf-8", newline="\n")
    print(f"wrote {OUT} ({len(html) // 1024} KB)")


if __name__ == "__main__":
    main()
