// Smoke-test build/omega.wasm the way the bench drives it: stub WASI, init, sequencer on,
// render 4 s at 132 BPM with the groove + GEN + warehouse reverb up. node tests/test_wasm.mjs
import fs from "fs";
import { fileURLToPath } from "url";
const root = fileURLToPath(new URL("..", import.meta.url));
const NAMES = ["fd_close", "fd_fdstat_get", "fd_fdstat_set_flags", "fd_prestat_get", "fd_prestat_dir_name", "fd_read",
  "fd_readdir", "fd_seek", "fd_write", "path_filestat_get", "path_open", "proc_exit"];
const wasi = {}; NAMES.forEach((n) => { wasi[n] = n === "proc_exit" ? (c) => { throw new Error("exit " + c); } : () => 8; });
const x = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(root + "build/omega.wasm")), { wasi_snapshot_preview1: wasi }).exports;
if (x._initialize) x._initialize();
if (x.wa_init(44100) !== 1) throw new Error("wa_init failed");
const enc = new TextEncoder(), dec = new TextDecoder();
function put(ptr, cap, s) { const m = new Uint8Array(x.memory.buffer), b = enc.encode(s); m.set(b.subarray(0, cap - 1), ptr); m[ptr + Math.min(b.length, cap - 1)] = 0; }
function set(k, v) { put(x.wa_key(), x.wa_key_cap(), k); put(x.wa_val(), x.wa_val_cap(), String(v)); x.wa_set(); }
function get(k) { put(x.wa_key(), x.wa_key_cap(), k); const n = x.wa_get(); return n < 0 ? null : dec.decode(new Uint8Array(x.memory.buffer, x.wa_out(), n)); }
const ui = JSON.parse(get("ui_hierarchy")), cp = JSON.parse(get("chain_params"));
for (const [k, v] of Object.entries({ grv_vol: 0.5, grv_genvol: 0.6, grv_rvmix: 0.4, grv_gpattern: 6, grv_gvoice: 1 })) set(k, v);
x.wa_seq(0x8888, 0x8000); x.wa_set_tempo(132, 1);
let e = 0, pk = 0, steps = new Set(), bad = 0;
for (let b = 0; b < 4 * 44100 / 128; b++) {
  x.wa_render(128); steps.add(x.wa_seq_step());
  const pcm = new Int16Array(x.memory.buffer, x.wa_pcm(), 256);
  for (const s of pcm) { const v = s / 32768; e += v * v; pk = Math.max(pk, Math.abs(v)); if (!isFinite(v)) bad++; }
}
const rms = Math.sqrt(e / (4 * 44100 * 2));
console.log(`wasm: ${Object.keys(ui.levels).length} levels, ${cp.length} chain_params, rms=${rms.toFixed(3)} peak=${pk.toFixed(3)} steps=${steps.size} view=${get("grv_gview")} step=${get("grv_gstep")}`);
if (rms < 0.02 || steps.size < 16 || bad) { console.log("wasm: FAIL"); process.exit(1); }

/* Beat lock, end to end: restart the transport at 137 BPM and run 32 bars. After every
 * block GEN's pattern step must be the sequencer's 16th (both read the same beat). */
x.wa_set_tempo(137, 0); x.wa_render(128); x.wa_set_tempo(137, 1);
let miss = 0, n = 0;
for (let b = 0; b < 32 * 4 * 60 / 137 * 44100 / 128; b++) {
  x.wa_render(128);
  const g = Number(get("grv_gstep")), k = x.wa_seq_step();
  if (g !== k) miss++;
  n++;
}
console.log(`wasm: GEN vs kick step over 32 bars at 137 BPM: ${n - miss}/${n} blocks agree`);
if (miss) { console.log("wasm: FAIL (GEN drifted off the kick grid)"); process.exit(1); }
console.log("wasm: OK");

/* SEQ voices: the Seq Voice page must show VOICE, that voice's own two knobs, then FILTER
 * (hidden keys drop out and the grid compacts, as on the host). Names come from chain_params. */
{
  const L = ui.levels.genseq, meta = {};
  cp.forEach((p) => { meta[p.key] = p; });
  const gate = {}; L.params.forEach((p) => { if (p.visible_if) gate[p.key] = p.visible_if; });
  const want = [["WAVE", "FOLD"], ["WAVE", "DETUNE"], ["WAVE", "FM AMT"], ["RES", "ENV AMT"], ["RES", "ENV AMT"],
    ["NOISE", "NOISE>FLT"], ["FM AMT", "NOISE>FLT"], ["WAVE", "BLIP"], ["WAVE", "CHORD"]];
  for (let vc = 0; vc < 9; vc++) {
    const vis = L.knobs.filter((k) => !gate[k] || gate[k].equals === vc);
    const names = vis.map((k) => meta[k].name);
    if (vis.length !== 8 || names[4] !== "SEQ VOICE" || names[5] !== want[vc][0] || names[6] !== want[vc][1] || names[7] !== "SEQ FILTER") {
      console.log("wasm: FAIL seq voice page", vc, names.join(" | ")); process.exit(1);
    }
  }
  console.log("wasm: Seq Voice page shows each voice's own two knobs (9/9)");
}
