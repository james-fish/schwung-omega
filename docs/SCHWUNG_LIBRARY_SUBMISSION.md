# Submitting Omega to the Schwung module library

Researched 2026-10-01. The Schwung catalog is in `charlesvestal/schwung`
(`module-catalog.json`, `catalog_version: 2`). Listing a module is a three-step
process (per the Schwung release workflow and existing module repos such as
`schwung-rex`, `schwung-obxd`, `schwung-dx7`).

## Steps

1. **Add `release.json` to this repo** (done — repo root):
   ```json
   {
     "version": "0.2.0",
     "download_url": "https://github.com/james-fish/schwung-omega/releases/download/v0.2.0/omega-module.tar.gz"
   }
   ```

2. **Publish a GitHub release** on `james-fish/schwung-omega` tagged `v0.2.0`,
   with an asset named **`omega-module.tar.gz`** containing the module folder
   (`module.json` + the aarch64 `dsp.so`). Build it with:
   ```bash
   # inside the schwung-builder image
   make dist        # produces build/omega-module.tar.gz
   gh release create v0.2.0 build/omega-module.tar.gz -t "Omega v0.2.0" -n "..."
   ```
   The CI workflow already builds the aarch64 `dsp.so`; `make dist` wraps it +
   `module.json` into the tarball under an `omega/` directory.

3. **Open a PR to `charlesvestal/schwung`** adding this entry to the `modules`
   array in `module-catalog.json`:
   ```json
   {
     "id": "omega",
     "name": "Omega",
     "description": "Multi-engine techno kick synth + feedback-free groove rumble + generative bass/bleep sequencer + performer glue",
     "author": "James Fish",
     "component_type": "sound_generator",
     "subcategory": "drum",
     "tags": ["techno", "kick", "rumble", "generative"],
     "github_repo": "james-fish/schwung-omega",
     "default_branch": "main",
     "asset_name": "omega-module.tar.gz",
     "min_host_version": "0.3.0"
   }
   ```
   Confirm `subcategory` against the catalog's `taxonomy` block (use the existing
   drum/kick subcategory name) and set `min_host_version` to the host version you
   test against.

## Status

- ✅ `release.json`, `make dist`, and the catalog entry prepared in-repo.
- ✅ **GitHub release `v0.2.0` published** — https://github.com/james-fish/schwung-omega/releases/tag/v0.2.0 (asset `omega-module.tar.gz`). `release.json`'s `download_url` resolves.
- ✅ **Catalog PR opened** → https://github.com/charlesvestal/schwung/pull/585 (adds the entry to `module-catalog.json`). Awaiting maintainer review; may request `subcategory` / `min_host_version` / description tweaks.

## DR32 engine selector — finding

Omega not appearing in DrumRack32's engine list is **expected and not fixable
from Omega's `module.json`**. DR32 (`legsmechanical/schwung-dr32`) ships a
**hardcoded** engine set (Sample, Simian, Urchin, 9W9, 6W6, 8W8, CW-78, ChowKick,
FM) compiled into `dsp/engines/` with a generated `engine_ui.json`. The `ENGN`
selector is populated from that compiled list, not by scanning modules or reading
a capability flag. To make Omega selectable as a DR32 pad engine you would need
to either:

1. contribute Omega to `schwung-dr32` as a built-in engine (add it to
   `dsp/engines/` + `engine_ui.json` and rebuild DR32), or
2. wait for / request DR32 support for loading arbitrary `plugin_api_v2_t`
   modules by id.

Omega already loads fine as a full module in **Schwung instrument slots** and
**Movy tracks**, which host any `plugin_api_v2_t` module dynamically.

## Sources

- https://github.com/charlesvestal/schwung (module-catalog.json, release workflow)
- https://github.com/charlesvestal/schwung-rex (release.json example)
- https://github.com/legsmechanical/schwung-dr32 (hardcoded engine set)
- https://schwung.dev/faqs.html
