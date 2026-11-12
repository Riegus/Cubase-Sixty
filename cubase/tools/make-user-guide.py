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

CSS = """
:root { --bg:#fbfbfa; --fg:#1d2126; --mut:#5b6470; --line:#dcdfe3; --card:#fff; --acc:#0a7d5a; --head:#f1f3f4; --code:#eceef0; }
@media (prefers-color-scheme: dark) {
  :root { --bg:#16181b; --fg:#e4e6e9; --mut:#9aa3ad; --line:#30353b; --card:#1d2024; --acc:#4fd1a5; --head:#24282d; --code:#262a2f; }
}
* { box-sizing: border-box; }
html { scroll-behavior: smooth; scroll-padding-top: 16px; }
body { margin:0; background:var(--bg); color:var(--fg); font:16px/1.6 "Segoe UI", system-ui, -apple-system, sans-serif; }
.wrap { display:flex; max-width:1240px; margin:0 auto; }
nav { flex:0 0 250px; position:sticky; top:0; align-self:flex-start; height:100vh; overflow-y:auto; padding:28px 12px 28px 20px;
      border-right:1px solid var(--line); font-size:14px; }
nav .ver { color:var(--mut); font-size:12px; margin-bottom:14px; }
nav ul { list-style:none; margin:0; padding:0; }
nav ul ul { padding-left:14px; margin-bottom:6px; }
nav ul ul ul a { font-size:13px; }
nav li { margin:2px 0; }
nav a { color:var(--mut); text-decoration:none; display:block; padding:1px 6px; border-radius:4px; }
nav > .toc > ul > li > a { display:none; }
nav > .toc > ul > li > ul { padding-left:0; }
nav > .toc > ul > li > ul > li > a { color:var(--fg); font-weight:600; margin-top:6px; }
nav a:hover { color:var(--acc); background:var(--head); }
main { flex:1; min-width:0; padding:28px 40px 80px; max-width:900px; }
h1 { font-size:34px; margin:0 0 12px; letter-spacing:-.01em; }
h2 { font-size:25px; margin:44px 0 12px; padding-bottom:6px; border-bottom:2px solid var(--acc); }
h3 { font-size:18px; margin:30px 0 8px; }
a { color:var(--acc); }
hr { border:0; border-top:1px solid var(--line); margin:40px 0; }
code { background:var(--code); padding:1px 5px; border-radius:4px; font:14px Consolas, "Cascadia Mono", monospace; }
.tbl { overflow-x:auto; margin:14px 0; }
table { border-collapse:collapse; width:100%; background:var(--card); font-size:15px; }
th, td { border:1px solid var(--line); padding:7px 10px; text-align:left; vertical-align:top; }
th { background:var(--head); font-weight:600; }
td:first-child { white-space:nowrap; }
li { margin:4px 0; }
@media (max-width: 860px) {
  .wrap { display:block; } nav { display:none; } main { padding:20px 16px 60px; }
  td:first-child { white-space:normal; }
}
@media print { nav { display:none; } main { max-width:none; padding:0; } body { background:#fff; color:#000; } }
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
    body = body.replace("<table>", '<div class="tbl"><table>').replace("</table>", "</table></div>")
    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cubase-Sixty user guide</title>
<style>{CSS}</style>
</head>
<body>
<div class="wrap">
<nav><div class="ver">Cubase-Sixty {version}</div>{md.toc}</nav>
<main>
{body}
</main>
</div>
</body>
</html>
"""
    OUT.write_text(html, encoding="utf-8", newline="\n")
    print(f"wrote {OUT} ({len(html) // 1024} KB)")


if __name__ == "__main__":
    main()
