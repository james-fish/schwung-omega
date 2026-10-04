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

## DR32 engine selector

DR32 (`legsmechanical/schwung-dr32`) used to ship a **hardcoded** engine set,
so Omega could not appear in its `ENGN` picker and nothing in Omega's
`module.json` could change that. That was the finding recorded here.

**From DR32 0.5.0 that is no longer true.** DR32 loads engines other modules
bring: it looks for a `dr32_engine.so` in its sibling module folders. Omega
builds one (`make dr32_engine.so`, from `src/dr32_engine.c`), offering eight of
its kick models as DR32 engines. See "Playing Omega's kicks in DR32" in the
README. The contract is DR32's `dsp/dr32_engine_api.h` (vendored here as
`src/dr32_engine_api.h`), documented in DR32's `docs/ENGINE_PLUGINS.md`.

⚠ The file is **built but not yet shipped**: `make dist`, CI and
`scripts/deploy.sh` still handle `dsp.so` alone. To ship it, `dist` must copy
`build/dr32_engine.so` into the tarball's `omega/` folder beside `dsp.so`.

Omega also loads as a full module in **Schwung instrument slots** and **Movy
tracks**, as before.

## Sources

- https://github.com/charlesvestal/schwung (module-catalog.json, release workflow)
- https://github.com/charlesvestal/schwung-rex (release.json example)
- https://github.com/legsmechanical/schwung-dr32 (hardcoded engine set)
- https://schwung.dev/faqs.html
