#!/usr/bin/env python3
"""check_chain_params.py — one-shot gate for the module.json chain_params array
(quick 261001-wft Task 1 / Finding 1 headline p-lock fix).

Asserts the host's parse_chain_params contract (charlesvestal/schwung
src/modules/chain/dsp/chain_params.c):
  - module.json parses as JSON and is <= 65536 bytes (host read cap)
  - NO top-level "ui_hierarchy" key (the host then uses chain_params)
  - a top-level "chain_params" array with >= 45 entries
  - each entry has "key" (<=31 chars), "name" (<=63 chars), "type" in
    {float,int,enum}; enums carry a non-empty "options" array
  - grv_gseqlen is declared type "int"
  - version bumped to 0.3.0 in module.json and release.json
Exits 0 on success, 1 with a diagnostic on any failure.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
MODULE = os.path.join(ROOT, "module.json")
RELEASE = os.path.join(ROOT, "release.json")

VALID_TYPES = {"float", "int", "enum"}
MAX_READ = 65536      # host caps the module.json read
MAX_KEY = 31          # key[32] in the host (31 chars + NUL)
MAX_NAME = 63         # name[64] in the host
MAX_PARAMS = 256      # MAX_CHAIN_PARAMS
MIN_ENTRIES = 45


def fail(msg):
    print(f"check_chain_params: FAIL — {msg}")
    sys.exit(1)


def main():
    raw = open(MODULE, "rb").read()
    if len(raw) > MAX_READ:
        fail(f"module.json is {len(raw)} bytes > {MAX_READ} host read cap")

    try:
        doc = json.loads(raw)
    except Exception as e:
        fail(f"module.json does not parse as JSON: {e}")

    if "ui_hierarchy" in doc:
        fail("module.json must NOT contain a top-level 'ui_hierarchy' key "
             "(it would make the host ignore chain_params)")

    if doc.get("version") != "0.3.0":
        fail(f"module.json version is {doc.get('version')!r}, expected '0.3.0'")

    cp = doc.get("chain_params")
    if not isinstance(cp, list):
        fail("module.json has no top-level 'chain_params' array")
    if len(cp) < MIN_ENTRIES:
        fail(f"chain_params has {len(cp)} entries, need >= {MIN_ENTRIES}")
    if len(cp) > MAX_PARAMS:
        fail(f"chain_params has {len(cp)} entries > host capacity {MAX_PARAMS}")

    seen = set()
    seqlen_ok = False
    for i, p in enumerate(cp):
        if not isinstance(p, dict):
            fail(f"entry {i} is not an object")
        key = p.get("key")
        name = p.get("name", p.get("label"))
        typ = p.get("type")
        if not isinstance(key, str) or not key:
            fail(f"entry {i} missing 'key'")
        if len(key) > MAX_KEY:
            fail(f"entry {i} key {key!r} exceeds {MAX_KEY} chars")
        if key in seen:
            fail(f"duplicate key {key!r}")
        seen.add(key)
        if not isinstance(name, str) or not name:
            fail(f"entry {key!r} missing 'name'/'label'")
        if len(name) > MAX_NAME:
            fail(f"entry {key!r} name exceeds {MAX_NAME} chars")
        if typ not in VALID_TYPES:
            fail(f"entry {key!r} type {typ!r} not in {sorted(VALID_TYPES)}")
        if typ == "enum":
            opts = p.get("options")
            if not isinstance(opts, list) or not opts:
                fail(f"enum entry {key!r} needs a non-empty 'options' array")
        if key == "grv_gseqlen":
            if typ != "int":
                fail("grv_gseqlen must be type 'int' (Finding 6)")
            if p.get("min") != 1 or p.get("max") != 64:
                fail("grv_gseqlen must be min 1 / max 64")
            seqlen_ok = True

    if not seqlen_ok:
        fail("grv_gseqlen entry not found in chain_params")

    # release.json version gate
    rel = json.loads(open(RELEASE, "rb").read())
    if rel.get("version") != "0.3.0":
        fail(f"release.json version is {rel.get('version')!r}, expected '0.3.0'")
    if "/v0.3.0/" not in rel.get("download_url", ""):
        fail("release.json download_url is not pinned to /v0.3.0/")

    print(f"check_chain_params: PASS — {len(cp)} entries, {len(raw)} bytes, "
          "no ui_hierarchy key, grv_gseqlen is int, versions 0.3.0")
    sys.exit(0)


if __name__ == "__main__":
    main()
