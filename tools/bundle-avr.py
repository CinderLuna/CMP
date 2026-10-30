"""Bundle pinned AVR8js CommonJS modules and the CMP Uno adapter; no npm execution."""
from pathlib import Path
import json
root = Path(__file__).resolve().parents[1]
modules = []
for path in sorted((root / "third_party/avr8js/dist/cjs").rglob("*.js")):
    key = path.relative_to(root / "third_party/avr8js/dist/cjs").as_posix()[:-3]
    modules.append(json.dumps(key) + ":function(module,exports,require){\n" + path.read_text(encoding="utf-8") + "\n}")
prefix = "// AVR8js 0.21.1, MIT, Copyright Uri Shaked and contributors. See third_party/avr8js/LICENSE.\nvar AVR=(function(){var modules={"
loader = "};var cache={};function load(id){if(cache[id])return cache[id].exports;var m={exports:{}};cache[id]=m;modules[id](m,m.exports,function(path){var parts=id.split('/');parts.pop();for(var p of path.split('/')){if(p==='..')parts.pop();else if(p!=='.')parts.push(p);}return load(parts.join('/'));});return m.exports;}return load('index');})();\n"
(root / "assets/cmp/runtime/avr.js").write_text(prefix + ",\n".join(modules) + loader + (root / "tools/uno-runtime.js").read_text(encoding="utf-8"), encoding="utf-8")
