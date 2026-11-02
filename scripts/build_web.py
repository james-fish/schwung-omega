#!/usr/bin/env python3
"""Fold the browser bench into ONE self-contained file: dist/omega.html.

Inlines build/omega.wasm (base64), web/worklet.js, the shared UI helpers and the module's own
canvas.js / cards.js, so the page opens straight from disk (file://) with no server.
Run scripts/build_web.sh (it compiles the wasm first)."""
import base64, json, os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
def read(*p):
    return open(os.path.join(ROOT, *p), encoding="utf-8").read()
wasm = base64.b64encode(open(os.path.join(ROOT, "build", "omega.wasm"), "rb").read()).decode()
font = re.search(r"  var FONT = \{.*?\n  \};", read("web", "font.js"), re.S).group(0)
app = read("web", "app.js").replace("/*@@FONT@@*/", font.strip())
scripts = "\n".join([
    read("src", "ui", "common.js"),
    read("canvas.js"),
    read("cards.js"),
    "var OMEGA_WORKLET = " + json.dumps(read("web", "worklet.js")) + ";",
    'var OMEGA_WASM_B64 = "' + wasm + '";',
    app,
])
assert "</script" not in scripts, "a script contains </script>"
html = read("web", "app.html").replace("/*@@STYLE@@*/", read("web", "style.css")).replace("/*@@SCRIPTS@@*/", scripts)
os.makedirs(os.path.join(ROOT, "dist"), exist_ok=True)
out = os.path.join(ROOT, "dist", "omega.html")
open(out, "w", encoding="utf-8").write(html)
print("wrote %s (%d KB)" % (out, len(html) // 1024))
