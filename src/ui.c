/* ui.c — Metadata-driven ui_hierarchy (v1.1 B1: UIX-02/03/05).
 *
 * Emits the host's ui_hierarchy schema into the caller's buffer with ZERO
 * allocation. v1.1 upgrade over the A-03 static fragments: every param now
 * carries rich metadata (type, short_name, default, min/max, step, unit) and
 * discrete params (MODEL, FX TYPE, groove MONO, GEN LPF POLE) render as `enum`
 * string selectors with an `options` array — matching the reference module.json
 * format the host expects, so knobs pick the right UI element and start position.
 *
 * Defaults are pulled from the params.c defaults tables (single source of truth,
 * no drift with the value cache). Numbers are formatted with pk_format_value
 * (locale-independent — never libc %f). Formatting into the caller buffer is
 * CPU-only (no malloc / IO / blocking), safe on the audio/UI thread.
 *
 * Page structure is owned here; the active model's Kick Page 2 interior is still
 * spliced from its vtable p2_slot_desc (B-09) between a fixed prefix and the FX
 * suffix. Groove Page 2 (`groove2`) is emitted ONLY for GEN (GRV-04). Every
 * write is bounded to buf_len; on overflow it returns what fit, null-terminated.
 */
#include "omega.h"
#include "params.h"
#include "dsp_primitives.h"   /* GROOT_NOTE_MAX (GEN root note-enum range) */

#include <stdio.h>
#include <string.h>

/* ---- bounded append (A-RESEARCH Pitfall 3) ------------------------------- */
static void ui_append(char *buf, int buf_len, int *off, const char *src, int len) {
    if (*off >= buf_len - 1) return;
    int room = (buf_len - 1) - *off;
    int n = len < room ? len : room;
    memcpy(buf + *off, src, (size_t)n);
    *off += n;
}
static void ui_puts(char *buf, int buf_len, int *off, const char *s) {
    ui_append(buf, buf_len, off, s, (int)strlen(s));
}

/* ---- param metadata ------------------------------------------------------ */
typedef enum { UP_FLOAT, UP_ENUM, UP_INT } up_type_t;
typedef struct {
    const char *key;
    const char *name;
    const char *shortn;      /* <=6 chars for the OLED */
    up_type_t   type;
    const char *unit;        /* "", "%", "Hz", "ms" ... */
    const char *step;        /* JSON number literal, e.g. "0.01" */
    const char *options;     /* enum: pre-serialized JSON array; else NULL */
    const char *mn, *mx;     /* float min/max JSON literals (NULL => 0.0/1.0) */
    const char *short_options; /* enum: short/code JSON array for the OLED grid;
                                * NULL => host draws `options`. Appended last so
                                * existing positional initializers default it to
                                * NULL without edits. */
    const char *vis;         /* optional pre-serialized visible_if object (per-param gate) */
} uiparam_t;

/* Default lookup: reuse the value-cache key->index so schema defaults never
 * drift from the runtime defaults (params.c). */
static float ui_default_for(const char *key) {
    int gi = pk_global_index(key);
    if (gi >= 0) return g_global_defaults[gi];
    int ki = pk_kick_index(key);
    if (ki >= 0) return g_kick_defaults[ki];
    return 0.0f;
}

/* Locale-independent parse of a small signed integer JSON literal (e.g. "1",
 * "64", "-32"). Used by UP_INT to map the normalized cache default across the
 * descriptor's integer [min,max] range. No libc atof/strtol (locale-dependent). */
static int ui_parse_int_literal(const char *s) {
    if (!s) return 0;
    int sign = 1, v = 0;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return sign * v;
}

/* Emit one param object into buf (bounded). Float params carry min/max/step/
 * unit; enum params carry options. Default comes from the params tables. */
static void ui_emit_param(char *buf, int buf_len, int *off, const uiparam_t *p) {
    char defbuf[24];
    char obj[640];
    if (p->type == UP_ENUM) {
        pk_format_value(ui_default_for(p->key), 0, defbuf, (int)sizeof defbuf);
        if (p->short_options)
            /* Spoken option names stay in `options`; the OLED grid draws the
             * short codes from `short_options` (A11Y-REBASE). */
            snprintf(obj, sizeof obj,
                "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
                "\"type\":\"enum\",\"options\":%s,\"short_options\":%s,\"default\":%s}",
                p->key, p->name, p->shortn, p->options, p->short_options, defbuf);
        else
            snprintf(obj, sizeof obj,
                "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
                "\"type\":\"enum\",\"options\":%s,\"default\":%s}",
                p->key, p->name, p->shortn, p->options, defbuf);
    } else if (p->type == UP_INT) {
        /* Finding 6: integer param — host shows whole numbers (no decimals).
         * min/max/step are integer JSON literals. The value cache stores the raw
         * NORMALIZED 0..1 knob position, so map the default across [min,max] and
         * round to a whole number (mirrors groove.c's 1+(v*63+0.5) seqlen map). */
        const char *mn = p->mn ? p->mn : "0";
        const char *mx = p->mx ? p->mx : "1";
        const char *st = p->step ? p->step : "1";
        /* Locale-independent integer literal parse (no atof/strtol). */
        float mnv = (float)ui_parse_int_literal(mn);
        float mxv = (float)ui_parse_int_literal(mx);
        /* v0.4.1: int params store the RAW integer in the cache (the host sends raw
         * integers for int params, like grv_grootnote). Emit it directly, clamped —
         * NOT the old `min + norm*(max-min)` map that assumed a normalized cache. */
        float di = ui_default_for(p->key);
        if (di < mnv) di = mnv; if (di > mxv) di = mxv;
        pk_format_value(di, 0, defbuf, (int)sizeof defbuf);
        snprintf(obj, sizeof obj,
            "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
            "\"type\":\"int\",\"min\":%s,\"max\":%s,\"default\":%s,"
            "\"step\":%s,\"unit\":\"%s\"}",
            p->key, p->name, p->shortn, mn, mx, defbuf, st, p->unit);
    } else {
        pk_format_value(ui_default_for(p->key), 4, defbuf, (int)sizeof defbuf);
        const char *mn = p->mn ? p->mn : "0.0";
        const char *mx = p->mx ? p->mx : "1.0";
        snprintf(obj, sizeof obj,
            "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
            "\"type\":\"float\",\"min\":%s,\"max\":%s,\"default\":%s,"
            "\"step\":%s,\"unit\":\"%s\"}",
            p->key, p->name, p->shortn, mn, mx, defbuf, p->step, p->unit);
    }
    if (p->vis) {                      /* gate: splice ,"visible_if":{...} before the closing brace */
        size_t n = strlen(obj);
        if (n > 0 && obj[n - 1] == '}')
            snprintf(obj + n - 1, sizeof obj - (n - 1), ",\"visible_if\":%s}", p->vis);
    }
    ui_puts(buf, buf_len, off, obj);
}

/* Emit a full level object: "<id>":{"name":"<name>","params":[...],"knobs":[...]}.
 * `knobs` is a pre-serialized JSON array literal. Params are comma-separated. */
/* `vis` is an optional pre-serialized visible_if JSON object (e.g.
 * {"param":"grv_type","equals":1}) or NULL. It is BOTH a replan trigger and a
 * gate: the new Schwung host only re-reads the dynamic ui_hierarchy when a key
 * named by SOME visible_if condition changes (page_controller replanIfCondition
 * / gateLevelsOf). Omega swaps which levels it emits on `model` / `grv_type`, so
 * without a visible_if naming those keys the host never re-reads and a picker
 * change leaves the old page's params on screen. We still emit ONE level set per
 * mode (level ids can't collide), and set equals to the CURRENT value so the
 * freshly-emitted level is always visible — the condition's real job is to make
 * the discriminator a gate key so the host replans and re-reads us. */
static void ui_emit_level(char *buf, int buf_len, int *off,
                          const char *id, const char *name,
                          const uiparam_t *params, int nparams,
                          const char *knobs, const char *vis) {
    char head[160];
    if (vis)
        snprintf(head, sizeof head,
                 "\"%s\":{\"name\":\"%s\",\"visible_if\":%s,\"params\":[", id, name, vis);
    else
        snprintf(head, sizeof head, "\"%s\":{\"name\":\"%s\",\"params\":[", id, name);
    ui_puts(buf, buf_len, off, head);
    for (int i = 0; i < nparams; i++) {
        if (i) ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &params[i]);
    }
    ui_puts(buf, buf_len, off, "],\"knobs\":");
    ui_puts(buf, buf_len, off, knobs);
    ui_puts(buf, buf_len, off, "}");
}

/* ---- enum option lists (.rodata) ----------------------------------------- */
/* A11Y-REBASE: `options` hold SPOKEN names (for screen readers / the list + enum
 * peek view); `OPT_*_SHORT` hold the original code arrays wired via short_options
 * so the OLED grid square still draws the compact codes. */
static const char OPT_MODEL[] =
    "[\"FM 2-Op\",\"FM 4-Op\",\"Wavetable\",\"Physical\",\"Hard\",\"Digital\","
    "\"Transient\",\"Analog\",\"User Sample\"]";
static const char OPT_MODEL_SHORT[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\"]";
static const char OPT_FX[]       = "[\"Diode\",\"Clip\",\"Saturate\",\"Fold\",\"Crush\"]";
static const char OPT_FX_SHORT[] = "[\"Diode\",\"Clip\",\"SAT\",\"Fold\",\"Crush\"]";
static const char OPT_MONO[]  = "[\"Stereo\",\"Mono\"]";
static const char OPT_POLE[]  = "[\"2-pole\",\"4-pole\"]";
static const char OPT_ROUTE[] = "[\"Synth\",\"Transient\",\"Both\"]";
static const char OPT_GRVTYPE[] = "[\"Rumble\",\"Seq\"]";
static const char OPT_GFILT[]   = "[\"LP\",\"HP\",\"Off\"]";
static const char OPT_RVTYPE[]  = "[\"Warehouse\",\"Concrete\",\"Tunnel\",\"Dirt\"]";
static const char OPT_RVTYPE_SHORT[] = "[\"Whse\",\"Concr\",\"Tunnl\",\"Dirt\"]";
static const char OPT_GPAT[] = "[\"Random\",\"Up\",\"Down\",\"Ping-Pong\",\"Leap\",\"Pedal\",\"Offbeat\",\"Rolling\",\"Acid\",\"Call+Resp\",\"Stabs\",\"Octaves\"]";
static const char OPT_GPAT_SHORT[] = "[\"Rnd\",\"Up\",\"Down\",\"PiPo\",\"Leap\",\"Pedal\",\"OffB\",\"Roll\",\"Acid\",\"Call\",\"Stab\",\"Oct\"]";
static const char OPT_GVOICE[] = "[\"Basic\",\"Detune\",\"FM\",\"Acid Saw\",\"Acid Sq\",\"Dirt\",\"Dirt 2\",\"Bleep\",\"Stab\"]";
static const char OPT_GVOICE_SHORT[] = "[\"Basic\",\"Detun\",\"FM\",\"AcSaw\",\"AcSq\",\"Dirt\",\"Dirt2\",\"Bleep\",\"Stab\"]";
static const char OPT_GCHORD[] = "[\"5th+Oct\",\"Minor\",\"Sus4\",\"Min7\"]";
static const char OPT_SCALE[]   = "[\"Unquantized\",\"Chromatic\",\"Major\",\"Minor\","
    "\"Penta\",\"Dorian\",\"Phrygian\",\"Mixolydian\","
    "\"Hirajoshi\",\"Hungarian\",\"WholeTone\",\"Blues\",\"Diminished\"]";
static const char OPT_GWAVE[]   = "[\"Sine\",\"Tri\",\"Saw\",\"Square\",\"Digital\",\"Analog\"]";
/* RESET (v0.4.1, item 4): None, 1..8 Bar, On Note (indices 0..9). */
static const char OPT_RESET[]   = "[\"None\",\"1 Bar\",\"2 Bar\",\"3 Bar\",\"4 Bar\","
    "\"5 Bar\",\"6 Bar\",\"7 Bar\",\"8 Bar\",\"On Note\"]";
/* NOTE LEN (v0.4.1, item 4/5): GEN step division. Indices map to multipliers in
 * groove.c gen_notelen_to_mult: {6,4,3,2,1.5,1,0.5} sixteenths. */
static const char OPT_NOTELEN[] = "[\"1/4d\",\"1/4\",\"1/8d\",\"1/8\",\"1/16d\",\"1/16\",\"1/32\"]";
static const char OPT_ONOFF[]   = "[\"Off\",\"On\"]";
/* FX-ROUTE: groove FX order (GROOVE/DRIVE/REVERB) — item 6: Rmbl renamed Grv. */
static const char OPT_FXROUTE[] = "[\"Grv>Drv>Rev\",\"Grv>Rev>Drv\",\"Rev>Grv>Drv\",\"Drv>Grv>Rev\"]";

/* Performer chain page (Phase D, PERF-05): duck -> DJ filter -> clip. DJ FILT is
 * a bidirectional centered sweep (0.5 = neutral). */
static const uiparam_t P_PERF[] = {
    { PK_MASTER_VOL, "Master Volume",   "MVOL",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK,       "Duck",            "DUCK",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_REL,   "Duck Release",    "DKREL", UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_SMT,   "Duck Slew",       "SLEW",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_BS,    "Duck Frequency",  "FREQ",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DJ_FILT,    "DJ Filter",       "DJFLT", UP_FLOAT, "%", "0.01", NULL },
    { PK_DJ_RESO,    "DJ Resonance",    "DJRES", UP_FLOAT, "%", "0.01", NULL },
    { PK_CLIP,       "Compressor Drive","CMPDR", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_PERF[] =
    "[\"" PK_MASTER_VOL "\",\"" PK_DUCK "\",\"" PK_DUCK_REL "\",\"" PK_DUCK_SMT
    "\",\"" PK_DUCK_BS "\",\"" PK_DJ_FILT "\",\"" PK_DJ_RESO "\",\"" PK_CLIP "\"]";

/* ---- level tables -------------------------------------------------------- */
static const uiparam_t P_ROOT[] = {
    { PK_MODEL,      "Model",  "MODEL", UP_ENUM,  "",  "0", OPT_MODEL, NULL, NULL, OPT_MODEL_SHORT },
    { PK_MASTER_VOL, "Volume", "VOL",   UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_ROOT[] = "[\"" PK_MODEL "\",\"" PK_MASTER_VOL "\"]";

/* Kick Page 1 (B2 reorg, VOICE-04): the fun lives here — PITCH, LENGTH, CURVE
 * (LENGTH now folds in SUSTAIN, VOICE-03) followed by the ACTIVE model's unique
 * params (spliced from its p2_slot_desc). PITCH exposed in Hz (VOICE-01). */
static const uiparam_t P_KICK1[] = {
    { PK_PITCH,   "Pitch",   "PITCH", UP_FLOAT, "Hz", "1",    NULL, "30", "200" },
    { PK_LENGTH,  "Length",  "LEN",   UP_FLOAT, "%",  "0.01", NULL },
    { PK_CURVE,   "Curve",   "CURVE", UP_FLOAT, "%",  "0.01", NULL },
};

/* Kick Page 2 (B2 reorg, VOICE-04): static shared set — the 3 transients, the
 * body FILTER (=COLOR) with routing, and the post FX (type/amount/tone). TRS TNE
 * shapes the transient; FILTER + ROUTE replace the old separate COLOR (VOICE-03/
 * 05). PITCH is in Hz but PK_PITCH still parses 0..1 internally in Phase B2-01;
 * the Hz domain conversion lands with the per-model voicing pass (B2-02). */
static const uiparam_t P_KICK2[] = {
    { PK_ATTACK,       "Attack",          "ATK",   UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_DEC,      "Transient Decay", "TRSDEC",UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_TNE,      "Transient Tone",  "TRSTNE",UP_FLOAT, "%", "0.01", NULL },
    { PK_COLOR,        "Filter",          "FILT",  UP_FLOAT, "%", "0.01", NULL },
    { PK_FILTER_ROUTE, "Filter Route",    "RTE",   UP_ENUM,  "",  "0",    OPT_ROUTE },
    { PK_FX_TYPE,      "FX Type",         "FXTYPE",UP_ENUM,  "",  "0",    OPT_FX, NULL, NULL, OPT_FX_SHORT },
    { PK_FX_AMT,       "FX Amount",       "FXAMT", UP_FLOAT, "%", "0.01", NULL },
    { PK_FX_TONE,      "FX Tone",         "FXTONE",UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_KICK2[] =
    "[\"" PK_ATTACK "\",\"" PK_TRS_DEC "\",\"" PK_TRS_TNE "\",\"" PK_COLOR
    "\",\"" PK_FILTER_ROUTE "\",\"" PK_FX_TYPE "\",\"" PK_FX_AMT "\",\"" PK_FX_TONE "\"]";

/* Groove Page 1 (C1 + vhr redesign): leads with the TYPE selector (TAPS/GEN,
 * GRVX-01), then the rumble controls. LENGTH is now a BIDIRECTIONAL clean<->drone
 * morph (vhr): RIGHT = clean equal-level kick copies on every 16th (feedback=0),
 * LEFT = a smeared/diffused resonant feedback drone (no longer per-tap decay).
 * MONO moved to Groove Page 2 to keep Page 1 at 8 encoders. */
/* v0.4: TYPE selector removed (TAPS + GEN run together). TAPS page = its own VOL
 * (default 0) + the FIR rumble controls. */
/* v0.4.1 (items 12/13): top row TVOL / DECAY / HPF / LPF, bottom row TAP1..4 (so the
 * taps line up on the lower encoders). VOL shows "TAP VOL" up top, short "TVOL". The
 * new HPF sits before the LPF in the signal chain and the UI. */
static const uiparam_t P_GROOVE1[] = {
    { PK_GRV_VOL,    "Rumble Vol", "RMBVOL",UP_FLOAT, "%", "0.01", NULL, "0.0", "2.0" },  /* past noon: drive */
    { PK_GRV_LENGTH, "Decay",      "DECAY",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_HPF,    "High-Pass",  "HPF",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_COLOR,  "Low-Pass",   "LPF",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP1,   "TAP1",   "TAP1", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP2,   "TAP2",   "TAP2", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP3,   "TAP3",   "TAP3", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP4,   "TAP4",   "TAP4", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_GROOVE1[] =
    "[\"" PK_GRV_VOL "\",\"" PK_GRV_LENGTH "\",\"" PK_GRV_HPF "\",\"" PK_GRV_COLOR
    "\",\"" PK_GRV_TAP1 "\",\"" PK_GRV_TAP2 "\",\"" PK_GRV_TAP3 "\",\"" PK_GRV_TAP4 "\"]";

/* P_GROOVE_FX: shared "Groove Effects" page — identical layout for TAPS and GEN
 * (E3/SC5). TAPS uses it as groove2; GEN uses it as groove3.
 * REVERB (vhr §B.2): the RV MIX knob is now BIDIRECTIONAL — CENTER = off,
 * LEFT = pre-smear (reverb into the tap ring input), RIGHT = post reverb. Same
 * key (PK_GRV_RVMIX), one Schroeder instance; the label communicates the range. */
/* Shared "Groove Effects" page (8 knobs) — identical for TAPS and GEN. DRIVE is
 * a diode drive; the LFO now sweeps tap LPF + RV TONE together; REVERB is a plain
 * dry/wet MIX; ROUTE picks the {rumble,drive,reverb} order; RV TYPE is a real
 * Room/Hall/Plate flavor (same cost). GEN's LP filter lives on its Gen Seq page
 * (next to WAVE/FOLD), TAPS's on its Page-1 LPF — so neither needs a FILTER here. */
/* v0.4.2: ROUTE moved to slot 4 (top row) so all four reverb controls (REVERB,
 * RV DECAY, RV TONE, RV TYPE) sit together on the bottom row. */
static const uiparam_t P_GROOVE_FX[] = {
    { PK_GRV_DRIVE,   "DRIVE",   "DRIVE", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LFOSPD,  "LFO SPD", "LFOSPD",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LFOAMT,  "LFO AMT", "LFOAMT",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_ROUTE,   "ROUTE",   "ROUTE", UP_ENUM,  "",  "0",    OPT_FXROUTE },
    { PK_GRV_RVMIX,   "REVERB",  "REV",   UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVDECAY, "RV DECAY","RVDEC", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVTONE,  "RV TONE", "RVTONE",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVTYPE,  "RV TYPE", "RVTYPE",UP_ENUM,  "",  "0",    OPT_RVTYPE, NULL, NULL, OPT_RVTYPE_SHORT },
};
static const char KN_GROOVE_FX[] =
    "[\"" PK_GRV_DRIVE "\",\"" PK_GRV_LFOSPD "\",\"" PK_GRV_LFOAMT
    "\",\"" PK_GRV_ROUTE "\",\"" PK_GRV_RVMIX "\",\"" PK_GRV_RVDECAY
    "\",\"" PK_GRV_RVTONE "\",\"" PK_GRV_RVTYPE "\"]";

/* v0.6.2 Space: per-source sends into the shared reverb (top row) and the synced
 * SEQ delay (bottom row). The Effects page REVERB knob is the reverb's return. */
static const char OPT_DLYTIME[] = "[\"1/16\",\"1/8T\",\"1/8\",\"1/8D\",\"1/4\",\"1/4D\"]";
static const uiparam_t P_SPACE[] = {
    { PK_GRV_RVRMB,   "RMB>REV",  "R>REV", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVSEQ,   "SEQ>REV",  "S>REV", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVKICK,  "KICK>REV", "K>REV", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVDLY,   "DLY>REV",  "D>REV", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_DLYSEND, "SEQ DLY",  "S>DLY", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_DLYTIME, "DLY TIME", "DTIME", UP_ENUM,  "",  "0",    OPT_DLYTIME },
    { PK_GRV_DLYFB,   "DLY FDBK", "DFDBK", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_DLYTONE, "DLY TONE", "DTONE", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_SPACE[] =
    "[\"" PK_GRV_RVRMB "\",\"" PK_GRV_RVSEQ "\",\"" PK_GRV_RVKICK "\",\"" PK_GRV_RVDLY
    "\",\"" PK_GRV_DLYSEND "\",\"" PK_GRV_DLYTIME "\",\"" PK_GRV_DLYFB "\",\"" PK_GRV_DLYTONE "\"]";

/* GEN groove page 2: sequence controls (SCALE/ROOT/RANGE/RETRIG moved to groove1). */
/* Gen Seq (8 knobs). iter-2: SEQ LEN shows 1–64; ROTATE is bidirectional −32..+32;
 * SEED is stepped (discrete patterns); SWING is replaced by DECAY (gen note
 * length); WAVE is a continuous scan (sine→…→analog + fold), not a discrete enum;
 * and the GEN LP FILTER lives here next to WAVE/FOLD (moved off the FX page).
 * v0.6.1: row 2 is the sound row VOICE / WAVE / FOLD / FILTER; DECAY moved to Gen Variation. */
#define VIS_V(n) "{\"param\":\"" PK_GRV_GVOICE "\",\"equals\":" #n "}"
#define VK(key, nm, sn, v) { key, nm, sn, UP_FLOAT, "%", "0.01", NULL, NULL, NULL, NULL, VIS_V(v) }
static const uiparam_t P_GROOVE_GEN_SEQ[] = {
    { PK_GRV_GSEED,    "SEED",    "SEED",  UP_FLOAT, "",  "0.0079", NULL, "0",   "127" },
    { PK_GRV_GSEQLEN,  "SEQ LEN", "SEQLEN",UP_INT,   "",  "1",      NULL, "1",   "64"  },
    { PK_GRV_GDENSITY, "DENSITY", "DENS",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GROTATE,  "ROTATE",  "ROT",   UP_FLOAT, "",  "0.0156", NULL, "-32", "32"  },
    { PK_GRV_GVOICE,   "VOICE",   "VOICE", UP_ENUM,  "",  "0",    OPT_GVOICE, NULL, NULL, OPT_GVOICE_SHORT },
    /* each voice's own A (main control) + B (character), shown only for that voice */
    VK(PK_GRV_GWAVE,    "WAVE",      "WAVE",  0), VK(PK_GRV_GFOLD,   "FOLD",      "FOLD",  0),
    VK(PK_GRV_GDTWAVE,  "WAVE",      "WAVE",  1), VK(PK_GRV_GDETUNE, "DETUNE",    "DETUNE",1),
    VK(PK_GRV_GFMWAVE,  "WAVE",      "WAVE",  2), VK(PK_GRV_GFMAMT,  "FM AMT",    "FM",    2),
    VK(PK_GRV_GASRES,   "RES",       "RES",   3), VK(PK_GRV_GASENV,  "ENV AMT",   "ENV",   3),
    VK(PK_GRV_GAQRES,   "RES",       "RES",   4), VK(PK_GRV_GAQENV,  "ENV AMT",   "ENV",   4),
    VK(PK_GRV_GD1NOISE, "NOISE",     "NOISE", 5), VK(PK_GRV_GD1NFM,  "NOISE>FLT", "N>FLT", 5),
    VK(PK_GRV_GD2FM,    "FM AMT",    "FM",    6), VK(PK_GRV_GD2NFM,  "NOISE>FLT", "N>FLT", 6),
    VK(PK_GRV_GBLWAVE,  "WAVE",      "WAVE",  7), VK(PK_GRV_GBLIP,   "BLIP",      "BLIP",  7),
    VK(PK_GRV_GSTWAVE,  "WAVE",      "WAVE",  8),
    { PK_GRV_GCHORD,   "CHORD",   "CHORD", UP_ENUM,  "",  "0",    OPT_GCHORD, NULL, NULL, NULL, VIS_V(8) },
    { PK_GRV_GENFILT,  "FILTER",  "FILT",  UP_FLOAT, "%", "0.01",   NULL },  /* the voice's own filter */
};
static const char KN_GROOVE_GEN_SEQ[] =
    "[\"" PK_GRV_GSEED "\",\"" PK_GRV_GSEQLEN "\",\"" PK_GRV_GDENSITY
    "\",\"" PK_GRV_GROTATE "\",\"" PK_GRV_GVOICE
    "\",\"" PK_GRV_GWAVE "\",\"" PK_GRV_GFOLD "\",\"" PK_GRV_GDTWAVE "\",\"" PK_GRV_GDETUNE
    "\",\"" PK_GRV_GFMWAVE "\",\"" PK_GRV_GFMAMT "\",\"" PK_GRV_GASRES "\",\"" PK_GRV_GASENV
    "\",\"" PK_GRV_GAQRES "\",\"" PK_GRV_GAQENV "\",\"" PK_GRV_GD1NOISE "\",\"" PK_GRV_GD1NFM
    "\",\"" PK_GRV_GD2FM "\",\"" PK_GRV_GD2NFM "\",\"" PK_GRV_GBLWAVE "\",\"" PK_GRV_GBLIP
    "\",\"" PK_GRV_GSTWAVE "\",\"" PK_GRV_GCHORD
    "\",\"" PK_GRV_GENFILT "\"]";

/* Gen Variation (v0.6.1): arp-style pattern + note shape + per-step dice. Row 1 is the
 * pattern necklace (custom:oneck) and DECAY's pluck; row 2 the dice strip (custom:odice).
 * VOICE moved to Gen Seq, next to WAVE/FOLD: it is how many oscillators play and where,
 * WAVE is the shape every one of them uses. */
static const uiparam_t P_GROOVE_GEN_VAR[] = {
    { PK_GRV_GPATTERN, "PATTERN", "PATT",  UP_ENUM,  "",  "0",    OPT_GPAT, NULL, NULL, OPT_GPAT_SHORT },
    { PK_GRV_GACCENT,  "ACCENT",  "ACCNT", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_GOCTJUMP, "OCT JUMP","OCTJ",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_GSWING,   "DECAY",   "DECAY", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_GRATCHET, "RATCHET", "RATCH", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_GSLIDE,   "SLIDE",   "SLIDE", UP_FLOAT, "%", "0.01", NULL },   /* acid voices; replaced DROP */
    { PK_GRV_GNRAND,   "NOTE RND","NRND",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_GLOOP,    "LOOP",    "LOOP",  UP_INT,   "",  "1",    NULL, "0", "16" },
};
static const char KN_GROOVE_GEN_VAR[] =
    "[\"" PK_GRV_GPATTERN "\",\"" PK_GRV_GACCENT "\",\"" PK_GRV_GOCTJUMP
    "\",\"" PK_GRV_GSWING "\",\"" PK_GRV_GRATCHET "\",\"" PK_GRV_GSLIDE
    "\",\"" PK_GRV_GNRAND "\",\"" PK_GRV_GLOOP "\"]";

/* (Legacy MODEL_GEN-kick groove2 page removed: GEN is not a selectable kick
 * model — OPT_MODEL is FM2..USR — so that branch was unreachable.) */

/* ---- wrapper fragments --------------------------------------------------- */
/* A11Y-REBASE: dropped the root child-index param key — get_param never served
 * that key, so every host read of it failed. The root object is just
 * pad_layout + levels now. */
static const char UI_OPEN[] =
    "{\"pad_layout\":\"drums\",\"levels\":{";
/* root has two nav links to the kick sub-pages appended before its knobs; keep
 * them as fixed fragments spliced into the root level. */
static const char UI_ROOT_LINKS[] =
    ",{\"level\":\"kick1\",\"label\":\"Kick 1\"},"
    "{\"level\":\"kick2\",\"label\":\"Kick 2\"}";
static const char UI_CLOSE[] = "}}";

#define NELEM(a) ((int)(sizeof(a)/sizeof((a)[0])))

/* Append each "key" value found in a spliced model interior to the knobs array
 * as ,"<key>" so Page 1's knob map covers the model-unique params too. The
 * interior is null-terminated (p2_slot_desc); bounded, no allocation. */
static void ui_emit_interior_keys(char *buf, int buf_len, int *off, const char *interior) {
    const char *p = interior;
    const char *tag = "\"key\":\"";
    while ((p = strstr(p, tag)) != NULL) {
        p += 7;                                   /* past the tag */
        const char *q = p;
        while (*q && *q != '"') q++;
        ui_puts(buf, buf_len, off, ",\"");
        ui_append(buf, buf_len, off, p, (int)(q - p));
        ui_puts(buf, buf_len, off, "\"");
        p = (*q) ? q + 1 : q;
    }
}

/* Emit the root level by hand (it mixes params with two nav-link objects). */
static void ui_emit_root(char *buf, int buf_len, int *off) {
    ui_puts(buf, buf_len, off, "\"root\":{\"name\":\"Omega\",\"params\":[");
    for (int i = 0; i < NELEM(P_ROOT); i++) {
        if (i) ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &P_ROOT[i]);
    }
    ui_puts(buf, buf_len, off, UI_ROOT_LINKS);
    ui_puts(buf, buf_len, off, "],\"knobs\":");
    ui_puts(buf, buf_len, off, KN_ROOT);
    ui_puts(buf, buf_len, off, "}");
}

/* Emit the ROOT note-name enum options: ["C-2","C#-2",...] for MIDI 0..GROOT_NOTE_MAX
 * (Ableton convention, MIDI 0 = C-2). Positive octaves have NO '+' sign; negative
 * octaves keep the '-' (on-device spec). Built into the caller buffer, bounded. */
static void ui_emit_root_note_options(char *buf, int buf_len, int *off) {
    static const char *const pc[12] =
        { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    ui_puts(buf, buf_len, off, "[");
    for (int n = 0; n <= GROOT_NOTE_MAX; n++) {
        char tmp[16];
        snprintf(tmp, sizeof tmp, "%s\"%s%d\"", n ? "," : "", pc[n % 12], n / 12 - 2);
        ui_puts(buf, buf_len, off, tmp);
    }
    ui_puts(buf, buf_len, off, "]");
}

/* v0.4 "Groove Gen" page 1 — GEN VOL / SCALE / ROOT (Hz) / ROOT NOTE / RANGE /
 * RETRIG. No TYPE selector and NO grv_type/grv_gscale gating (that LEVEL-gating
 * caused the host to re-plan and jump to the root page). Instead both root
 * controls are ALWAYS present as SEPARATE keys: grv_groot is a Hz float (used in
 * Unquantized mode) and grv_grootnote is a note-name enum (used in a scale) — no
 * dynamic descriptor swap, so the page never gets corrupted. */
static void ui_emit_gen_groove1(char *buf, int buf_len, int *off,
                                const bohm_instance_t *inst) {
    /* v0.4.1 layout — row1: GEN VOL, SCALE, ROOT, RANGE; row2: RESET, NOTE LEN,
     * SWING, GEN>TAPS. ROOT is a SINGLE merged Hz control (ROOT NOTE removed). */
    static const uiparam_t p_head[] = {
        { PK_GRV_GENVOL, "Seq Vol","SEQVOL",UP_FLOAT, "%","0.01", NULL,      "0.0", "2.0" },  /* past noon: drive */
        { PK_GRV_GSCALE, "SCALE",  "SCALE", UP_ENUM,  "", "0",    OPT_SCALE, NULL, NULL },
    };
    static const uiparam_t p_tail[] = {
        { PK_GRV_GRANGE,   "RANGE",   "RANGE", UP_FLOAT, "",  "0.04", NULL,        NULL, NULL },
        { PK_GRV_GRETRIG,  "RESET",   "RESET", UP_ENUM,  "",  "0",    OPT_RESET,   NULL, NULL },
        { PK_GRV_GNOTELEN, "NOTE LEN","NLEN",  UP_ENUM,  "",  "0",    OPT_NOTELEN, NULL, NULL },
        { PK_GRV_GSWINGAMT,"SWING",   "SWING", UP_FLOAT, "%", "0.01", NULL,        NULL, NULL },
        { PK_GRV_GENTAPS,  "SEQ>RMB", "S>RMB", UP_FLOAT, "%", "0.01", NULL,        NULL, NULL },
    };
    (void)inst;
    ui_puts(buf, buf_len, off, "\"gengroove1\":{\"name\":\"Sequence\",\"params\":[");
    for (int i = 0; i < (int)(sizeof p_head / sizeof p_head[0]); i++) {
        if (i) ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &p_head[i]);
    }
    /* ROOT (Hz float) — single merged control, used in both scale + unquantized modes. */
    ui_puts(buf, buf_len, off,
        ",{\"key\":\"" PK_GRV_GROOT "\",\"name\":\"ROOT\",\"short_name\":\"ROOT\","
        "\"type\":\"float\",\"min\":20,\"max\":520,\"default\":45,\"step\":1,\"unit\":\"Hz\"}");
    for (int i = 0; i < (int)(sizeof p_tail / sizeof p_tail[0]); i++) {
        ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &p_tail[i]);
    }
    ui_puts(buf, buf_len, off,
        "],\"knobs\":[\""
        PK_GRV_GENVOL "\",\"" PK_GRV_GSCALE "\",\"" PK_GRV_GROOT "\",\"" PK_GRV_GRANGE
        "\",\"" PK_GRV_GRETRIG "\",\"" PK_GRV_GNOTELEN "\",\"" PK_GRV_GSWINGAMT
        "\",\"" PK_GRV_GENTAPS "\"]}");
}

/* Append a model's p2_slot_desc interior (bare comma-separated FLAT param
 * objects; enum `options` use [] so every '}' closes exactly one object) into
 * buf, inserting ,"visible_if":{"param":"model","equals":N} before EACH closing
 * brace. This is what makes the kick page STATIC: every picker model's params
 * are declared once, and the host shows ONLY the active model's (filtering each
 * param on `model`) and re-plans when `model` changes. The host does NOT re-read
 * ui_hierarchy on a knob-driven enum change — it re-filters the CACHED hierarchy
 * by visible_if (page_controller replanNow) — so a per-model fixed gate is the
 * only thing that actually switches the page. */
static void ui_append_gated_interior(char *buf, int buf_len, int *off,
                                      const char *interior, int model_idx) {
    char gate[56];
    int glen = snprintf(gate, sizeof gate,
                        ",\"visible_if\":{\"param\":\"" PK_MODEL "\",\"equals\":%d}", model_idx);
    for (const char *p = interior; *p; p++) {
        if (*p == '}') ui_append(buf, buf_len, off, gate, glen);
        ui_append(buf, buf_len, off, p, 1);
    }
}

int omega_build_ui(bohm_instance_t *inst, char *buf, int buf_len) {
    if (!buf || buf_len <= 0) return 0;

    /* v0.4: the groove pages are NO LONGER gated by grv_type. TAPS and GEN run
     * simultaneously (independent VOLs), so all groove levels are ALWAYS visible.
     * Removing the grv_type LEVEL-gate is what stops the host re-plan that kept
     * jumping the view back to the root page on a TAPS/GEN switch. */

    int off = 0;
    ui_puts(buf, buf_len, &off, UI_OPEN);

    ui_emit_root(buf, buf_len, &off);

    /* kick1 (STATIC): PITCH/LENGTH/CURVE (always visible) + EVERY picker model's
     * unique params, each gated by visible_if model==N. The host filters to the
     * active model's params (hidden keys drop out of params AND the knobs grid,
     * which compacts — page_plan isHiddenParam) and re-plans on a model change.
     * knobs lists the fixed keys + every model's keys; the active model's compact
     * in after PITCH/LENGTH/CURVE. */
    ui_puts(buf, buf_len, &off, ",\"kick1\":{\"name\":\"Kick 1\",\"params\":[");
    for (int i = 0; i < NELEM(P_KICK1); i++) {
        if (i) ui_puts(buf, buf_len, &off, ",");
        ui_emit_param(buf, buf_len, &off, &P_KICK1[i]);
    }
    char scratch[1024];
    if (inst) {
        for (int m = 0; m < MODEL_GEN; m++) {          /* 0..8 = the 9 picker models */
            const kick_model_vtable_t *vt = g_models[m];
            if (!vt || !vt->p2_slot_desc) continue;
            int n = vt->p2_slot_desc(inst, scratch, (int)sizeof scratch);
            if (n <= 0 || n >= (int)sizeof scratch) continue;
            scratch[n] = '\0';
            ui_puts(buf, buf_len, &off, ",");
            ui_append_gated_interior(buf, buf_len, &off, scratch, m);
        }
    }
    ui_puts(buf, buf_len, &off, "],\"knobs\":[\"" PK_PITCH "\",\"" PK_LENGTH "\",\"" PK_CURVE "\"");
    if (inst) {
        for (int m = 0; m < MODEL_GEN; m++) {
            const kick_model_vtable_t *vt = g_models[m];
            if (!vt || !vt->p2_slot_desc) continue;
            int n = vt->p2_slot_desc(inst, scratch, (int)sizeof scratch);
            if (n <= 0 || n >= (int)sizeof scratch) continue;
            scratch[n] = '\0';
            ui_emit_interior_keys(buf, buf_len, &off, scratch);
        }
    }
    ui_puts(buf, buf_len, &off, "]}");

    /* kick2: static shared transient/FILTER/FX page (model-agnostic, no gate). */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "kick2", "Kick 2", P_KICK2, NELEM(P_KICK2), KN_KICK2, NULL);

    /* Groove pages (v0.4 — ALL ungated, always visible):
     *   groove1    "Rumble"       (RUMBLE VOL + FIR rumble controls)
     *   gengroove1 "Sequence"     (SEQ VOL/SCALE/ROOT/RANGE/RESET...)
     *   genseq     "Gen Seq"      (SEED/SEQLEN/.../GEN FILTER)
     *   groovefx   "Effects"      (shared drive/reverb/route) */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "groove1", "Rumble",
                  P_GROOVE1, NELEM(P_GROOVE1), KN_GROOVE1, NULL);
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_gen_groove1(buf, buf_len, &off, inst);          /* "gengroove1" "Sequence" */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "genseq", "Seq Voice",
                  P_GROOVE_GEN_SEQ, NELEM(P_GROOVE_GEN_SEQ), KN_GROOVE_GEN_SEQ, NULL);
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "genvar", "Seq Variation",
                  P_GROOVE_GEN_VAR, NELEM(P_GROOVE_GEN_VAR), KN_GROOVE_GEN_VAR, NULL);
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "groovefx", "Effects",
                  P_GROOVE_FX, NELEM(P_GROOVE_FX), KN_GROOVE_FX, NULL);
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "space", "Space",
                  P_SPACE, NELEM(P_SPACE), KN_SPACE, NULL);

    /* Performer chain page (Phase D) — always present, no gate. */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "perf1", "Performer", P_PERF, NELEM(P_PERF), KN_PERF, NULL);

    ui_puts(buf, buf_len, &off, UI_CLOSE);
    buf[off] = '\0';
    return off;
}
