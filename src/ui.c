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
    const char *shortopts;   /* enum: optional JSON array drawn in the 3-4 char
                              * square; `options` is what the screen reader says */
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

/* Emit one param object into buf (bounded). Float params carry min/max/step/
 * unit; enum params carry options. Default comes from the params tables. */
static void ui_emit_param(char *buf, int buf_len, int *off, const uiparam_t *p) {
    char defbuf[24];
    char obj[768];
    if (p->type == UP_ENUM) {
        pk_format_value(ui_default_for(p->key), 0, defbuf, (int)sizeof defbuf);
        snprintf(obj, sizeof obj,
            "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
            "\"type\":\"enum\",\"options\":%s%s%s,\"default\":%s}",
            p->key, p->name, p->shortn, p->options,
            p->shortopts ? ",\"short_options\":" : "", p->shortopts ? p->shortopts : "",
            defbuf);
    } else if (p->type == UP_INT) {
        pk_format_value(ui_default_for(p->key), 0, defbuf, (int)sizeof defbuf);
        snprintf(obj, sizeof obj,
            "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
            "\"type\":\"int\",\"min\":%s,\"max\":%s,\"default\":%s,"
            "\"step\":1,\"unit\":\"%s\"}",
            p->key, p->name, p->shortn, p->mn, p->mx, defbuf, p->unit);
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
    ui_puts(buf, buf_len, off, obj);
}

/* Emit a full level object: "<id>":{"name":"<name>","params":[...],"knobs":[...]}.
 * `knobs` is a pre-serialized JSON array literal. Params are comma-separated. */
static void ui_emit_level(char *buf, int buf_len, int *off,
                          const char *id, const char *name,
                          const uiparam_t *params, int nparams,
                          const char *knobs) {
    char head[96];
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
/* `options` are WORDS because they are what the screen reader speaks and what
 * the list view and enum peek print; the grid's square draws `short_options`. */
static const char OPT_MODEL[] =
    "[\"FM 2-Op\",\"FM 4-Op\",\"Wavetable\",\"Physical\",\"Hard\",\"Digital\","
    "\"Transistor\",\"Analog\",\"User\"]";
static const char SOPT_MODEL[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\"]";
static const char OPT_FX[]    = "[\"Diode\",\"Clip\",\"Saturate\",\"Fold\",\"Crush\"]";
static const char SOPT_FX[]   = "[\"Diode\",\"Clip\",\"Sat\",\"Fold\",\"Crush\"]";
static const char OPT_MONO[]  = "[\"Stereo\",\"Mono\"]";
static const char OPT_POLE[]  = "[\"2-pole\",\"4-pole\"]";
static const char OPT_ROUTE[] = "[\"Synth\",\"Transient\",\"Both\"]";
static const char SOPT_ROUTE[] = "[\"Syn\",\"Trn\",\"Both\"]";
static const char OPT_GRVTYPE[] = "[\"Taps\",\"Generative\"]";
static const char SOPT_GRVTYPE[] = "[\"Taps\",\"Gen\"]";
static const char OPT_GFILT[]   = "[\"LP\",\"HP\",\"Off\"]";
static const char OPT_RVTYPE[]  = "[\"Room\",\"Hall\",\"Plate\"]";
static const char OPT_SCALE[]   = "[\"Unquantized\",\"Chromatic\",\"Major\",\"Minor\","
    "\"Pentatonic\",\"Dorian\",\"Phrygian\",\"Mixolydian\","
    "\"Hirajoshi\",\"Hungarian\",\"Whole Tone\",\"Blues\",\"Diminished\"]";
static const char SOPT_SCALE[]  = "[\"Unq\",\"Chr\",\"Maj\",\"Min\",\"Pent\",\"Dor\",\"Phr\","
    "\"Mix\",\"Hira\",\"Hung\",\"Whl\",\"Blue\",\"Dim\"]";
static const char OPT_GWAVE[]   = "[\"Sine\",\"Tri\",\"Saw\",\"Square\",\"Digital\",\"Analog\"]";
static const char OPT_RETRIG[]  = "[\"None\",\"1 Bar\",\"2 Bar\",\"4 Bar\",\"8 Bar\",\"On Note\"]";
static const char OPT_ONOFF[]   = "[\"Off\",\"On\"]";
/* Phase 1 FX-ROUTE: groove FX order (RUMBLE/DRIVE/REVERB). */
static const char OPT_FXROUTE[] = "[\"Rumble-Drive-Reverb\",\"Rumble-Reverb-Drive\","
    "\"Reverb-Rumble-Drive\",\"Drive-Rumble-Reverb\"]";
static const char SOPT_FXROUTE[] = "[\"RDV\",\"RVD\",\"VRD\",\"DRV\"]";

/* Performer chain page (Phase D, PERF-05): duck -> DJ filter -> clip. DJ FILT is
 * a bidirectional centered sweep (0.5 = neutral). */
static const uiparam_t P_PERF[] = {
    { PK_MASTER_VOL, "Volume", "VOL",   UP_FLOAT, "%", "0.01", NULL },  /* identical to root: one key, one description */
    { PK_DUCK,       "Duck",     "DUCK",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_REL,   "Duck Release", "DKREL", UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_SMT,   "Duck Slew","SLEW",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_BS,    "Duck Frequency","FREQ",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DJ_FILT,    "DJ Filter","DJFLT", UP_FLOAT, "%", "0.01", NULL },
    { PK_DJ_RESO,    "DJ Resonance","DJRES", UP_FLOAT, "%", "0.01", NULL },
    { PK_CLIP,       "Compressor Drive","CMPDR", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_PERF[] =
    "[\"" PK_MASTER_VOL "\",\"" PK_DUCK "\",\"" PK_DUCK_REL "\",\"" PK_DUCK_SMT
    "\",\"" PK_DUCK_BS "\",\"" PK_DJ_FILT "\",\"" PK_DJ_RESO "\",\"" PK_CLIP "\"]";

/* ---- level tables -------------------------------------------------------- */
static const uiparam_t P_ROOT[] = {
    { PK_MODEL,      "Model",  "MODEL", UP_ENUM,  "",  "0", OPT_MODEL, NULL, NULL, SOPT_MODEL },
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
    { PK_ATTACK,       "Attack",   "ATK",   UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_DEC,      "Transient Decay","TRSDEC",UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_TNE,      "Transient Tone","TRSTNE",UP_FLOAT, "%", "0.01", NULL },
    { PK_COLOR,        "Filter",   "FILT",  UP_FLOAT, "%", "0.01", NULL },
    { PK_FILTER_ROUTE, "Filter Route","RTE", UP_ENUM,  "",  "0",    OPT_ROUTE, NULL, NULL, SOPT_ROUTE },
    { PK_FX_TYPE,      "FX Type",  "FXTYPE",UP_ENUM,  "",  "0",    OPT_FX, NULL, NULL, SOPT_FX },
    { PK_FX_AMT,       "FX Amount","FXAMT", UP_FLOAT, "%", "0.01", NULL },
    { PK_FX_TONE,      "FX Tone",  "FXTONE",UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_KICK2[] =
    "[\"" PK_ATTACK "\",\"" PK_TRS_DEC "\",\"" PK_TRS_TNE "\",\"" PK_COLOR
    "\",\"" PK_FILTER_ROUTE "\",\"" PK_FX_TYPE "\",\"" PK_FX_AMT "\",\"" PK_FX_TONE "\"]";

/* Groove Page 1 (C1 + vhr redesign): leads with the TYPE selector (TAPS/GEN,
 * GRVX-01), then the rumble controls. LENGTH is now a BIDIRECTIONAL clean<->drone
 * morph (vhr): RIGHT = clean equal-level kick copies on every 16th (feedback=0),
 * LEFT = a smeared/diffused resonant feedback drone (no longer per-tap decay).
 * MONO moved to Groove Page 2 to keep Page 1 at 8 encoders. */
static const uiparam_t P_GROOVE1[] = {
    { PK_GRV_TYPE,   "Groove Type","TYPE", UP_ENUM, "", "0",   OPT_GRVTYPE, NULL, NULL, SOPT_GRVTYPE },
    { PK_GRV_VOL,    "Groove Volume","VOL", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LENGTH, "Rumble Length","LEN",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_COLOR,  "Rumble Lowpass","LPF",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP1,   "Tap 1",  "TAP1", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP2,   "Tap 2",  "TAP2", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP3,   "Tap 3",  "TAP3", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP4,   "Tap 4",  "TAP4", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_GROOVE1[] =
    "[\"" PK_GRV_TYPE "\",\"" PK_GRV_VOL "\",\"" PK_GRV_LENGTH "\",\"" PK_GRV_COLOR
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
static const uiparam_t P_GROOVE_FX[] = {
    { PK_GRV_DRIVE,   "Drive",   "DRIVE", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LFOSPD,  "LFO Speed","LFOSPD",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LFOAMT,  "LFO Amount","LFOAMT",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVMIX,   "Reverb",  "REV",   UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVDECAY, "Reverb Decay","RVDEC", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVTONE,  "Reverb Tone","RVTONE",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_ROUTE,   "FX Order","ROUTE", UP_ENUM,  "",  "0",    OPT_FXROUTE, NULL, NULL, SOPT_FXROUTE },
    { PK_GRV_RVTYPE,  "Reverb Type","RVTYPE",UP_ENUM,  "",  "0",    OPT_RVTYPE },
};
static const char KN_GROOVE_FX[] =
    "[\"" PK_GRV_DRIVE "\",\"" PK_GRV_LFOSPD "\",\"" PK_GRV_LFOAMT
    "\",\"" PK_GRV_RVMIX "\",\"" PK_GRV_RVDECAY "\",\"" PK_GRV_RVTONE
    "\",\"" PK_GRV_ROUTE "\",\"" PK_GRV_RVTYPE "\"]";

/* GEN groove page 2: sequence controls (SCALE/ROOT/RANGE/RETRIG moved to groove1). */
/* Gen Seq (8 knobs). iter-2: SEQ LEN shows 1–64; ROTATE is bidirectional −32..+32;
 * SEED is stepped (discrete patterns); SWING is replaced by DECAY (gen note
 * length); WAVE is a continuous scan (sine→…→analog + fold), not a discrete enum;
 * and the GEN LP FILTER lives here next to WAVE/FOLD (moved off the FX page). */
static const uiparam_t P_GROOVE_GEN_SEQ[] = {
    { PK_GRV_GSEED,    "Seed",    "SEED",  UP_INT,   "",  "1",      NULL, "0",   "127" },
    { PK_GRV_GSEQLEN,  "Sequence Length","SEQLEN",UP_INT, "steps", "1", NULL, "1", "64" },
    { PK_GRV_GDENSITY, "Density", "DENS",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GROTATE,  "Rotate",  "ROT",   UP_INT,   "steps", "1", NULL, "-32", "32" },
    { PK_GRV_GSWING,   "Note Decay","DECAY", UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GWAVE,    "Wave",    "WAVE",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GFOLD,    "Fold",    "FOLD",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_COLOR,    "Lowpass", "FILT",  UP_FLOAT, "%", "0.01",   NULL },
};
static const char KN_GROOVE_GEN_SEQ[] =
    "[\"" PK_GRV_GSEED "\",\"" PK_GRV_GSEQLEN "\",\"" PK_GRV_GDENSITY
    "\",\"" PK_GRV_GROTATE "\",\"" PK_GRV_GSWING "\",\"" PK_GRV_GWAVE
    "\",\"" PK_GRV_GFOLD "\",\"" PK_GRV_COLOR "\"]";

static const uiparam_t P_GROOVE2[] = {
    { PK_GEN_SEED,    "SEED",    "SEED",  UP_FLOAT, "",  "0.01", NULL },
    { PK_GEN_SCALE,   "SCALE",   "SCALE", UP_FLOAT, "",  "0.01", NULL },
    { PK_GEN_SEQLEN,  "SEQ LEN", "SEQLEN",UP_FLOAT, "",  "0.01", NULL },
    { PK_GEN_LPFFREQ, "LPF FREQ","LPFFRQ",UP_FLOAT, "%", "0.01", NULL },
    { PK_GEN_LPFPOLE, "LPF POLE","LPFPOL",UP_ENUM,  "",  "0",    OPT_POLE },
    { PK_GEN_DENSITY, "DENSITY", "DENS",  UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_GROOVE2[] =
    "[\"" PK_GEN_SEED "\",\"" PK_GEN_SCALE "\",\"" PK_GEN_SEQLEN "\",\"" PK_GEN_LPFFREQ
    "\",\"" PK_GEN_LPFPOLE "\",\"" PK_GEN_DENSITY "\"]";

/* ---- wrapper fragments --------------------------------------------------- */
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

/* E3/SC4: GEN groove page 1 — TYPE/VOL/SCALE/ROOT/RANGE/RETRIG. ROOT is dynamic:
 * float Hz when unquantized, float 0..1 snapped to 84 note steps otherwise. */
static void ui_emit_gen_groove1(char *buf, int buf_len, int *off,
                                const bohm_instance_t *inst) {
    static const uiparam_t p_head[] = {
        { PK_GRV_TYPE,   "Groove Type","TYPE", UP_ENUM, "", "0",    OPT_GRVTYPE, NULL, NULL, SOPT_GRVTYPE },
        { PK_GRV_VOL,    "Groove Volume","VOL", UP_FLOAT, "%","0.01", NULL,     NULL, NULL },
        { PK_GRV_GSCALE, "Scale", "SCALE", UP_ENUM,  "", "0",    OPT_SCALE,   NULL, NULL, SOPT_SCALE },
    };
    static const uiparam_t p_tail[] = {
        { PK_GRV_GRANGE,  "Range",  "RANGE",  UP_INT,   "degrees", "1", NULL, "1", "24" },
        { PK_GRV_GRETRIG, "Retrigger", "RETRIG", UP_ENUM,  "", "0",    OPT_RETRIG, NULL, NULL },
    };
    /* ROOT is Hz in BOTH modes: groove.c maps it to the same 20..2000 Hz whatever
     * the scale (the scale only quantises the intervals above it). The quantized
     * branch used to declare 0..1, which the host then wrote verbatim. */
    (void)inst;
    const uiparam_t p_root =
        { PK_GRV_GROOT, "Root", "ROOT", UP_FLOAT, "Hz", "1", NULL, "20", "2000" };
    ui_puts(buf, buf_len, off,
        "\"groove1\":{\"name\":\"Gen Groove\",\"params\":[");
    for (int i = 0; i < (int)(sizeof p_head / sizeof p_head[0]); i++) {
        if (i) ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &p_head[i]);
    }
    ui_puts(buf, buf_len, off, ",");
    ui_emit_param(buf, buf_len, off, &p_root);
    for (int i = 0; i < (int)(sizeof p_tail / sizeof p_tail[0]); i++) {
        ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &p_tail[i]);
    }
    ui_puts(buf, buf_len, off,
        "],\"knobs\":[\""
        PK_GRV_TYPE "\",\"" PK_GRV_VOL "\",\"" PK_GRV_GSCALE "\",\""
        PK_GRV_GROOT "\",\"" PK_GRV_GRANGE "\",\"" PK_GRV_GRETRIG "\"]}");
}

int omega_build_ui(bohm_instance_t *inst, char *buf, int buf_len) {
    if (!buf || buf_len <= 0) return 0;

    /* Active model's Kick Page 2 interior (bare comma-separated slot objects). */
    char scratch[1024];
    int  slot_len = 0;
    if (inst) {
        const kick_model_vtable_t *vt = g_models[inst->model];
        if (vt && vt->p2_slot_desc) {
            slot_len = vt->p2_slot_desc(inst, scratch, (int)sizeof scratch);
            if (slot_len < 0 || slot_len >= (int)sizeof scratch) slot_len = 0;
        }
    }

    int off = 0;
    ui_puts(buf, buf_len, &off, UI_OPEN);

    ui_emit_root(buf, buf_len, &off);

    /* kick1 (B2 reorg): PITCH/LENGTH/CURVE + the active model's unique params
     * (spliced from p2_slot_desc). knobs = those fixed keys + the model keys
     * extracted from the interior, so all 8 encoders map correctly. */
    ui_puts(buf, buf_len, &off, ",\"kick1\":{\"name\":\"Kick 1\",\"params\":[");
    for (int i = 0; i < NELEM(P_KICK1); i++) {
        if (i) ui_puts(buf, buf_len, &off, ",");
        ui_emit_param(buf, buf_len, &off, &P_KICK1[i]);
    }
    if (slot_len > 0) {
        ui_puts(buf, buf_len, &off, ",");
        ui_append(buf, buf_len, &off, scratch, slot_len);
    }
    ui_puts(buf, buf_len, &off, "],\"knobs\":[\"" PK_PITCH "\",\"" PK_LENGTH "\",\"" PK_CURVE "\"");
    if (slot_len > 0) ui_emit_interior_keys(buf, buf_len, &off, scratch);
    ui_puts(buf, buf_len, &off, "]}");

    /* kick2 (B2 reorg): static shared transient/FILTER/FX page. */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "kick2", "Kick 2", P_KICK2, NELEM(P_KICK2), KN_KICK2);

    /* Groove pages (E3 layout):
     *   - GEN groove type: groove1=Gen Groove (SCALE/ROOT/RANGE/RETRIG), groove2=Gen Seq,
     *                      groove3="Groove Effects" (shared FX)
     *   - TAPS groove type: groove1=Groove 1 (shared), groove2="Groove Effects" (shared FX)
     *   - MODEL_GEN kick (legacy): shared groove1 + legacy groove2 */
    if (inst && inst->groove.type == GROOVE_TYPE_GEN) {
        ui_puts(buf, buf_len, &off, ",");
        ui_emit_gen_groove1(buf, buf_len, &off, inst);
        ui_puts(buf, buf_len, &off, ",");
        ui_emit_level(buf, buf_len, &off, "groove2", "Gen Seq",
                      P_GROOVE_GEN_SEQ, NELEM(P_GROOVE_GEN_SEQ), KN_GROOVE_GEN_SEQ);
        ui_puts(buf, buf_len, &off, ",");
        ui_emit_level(buf, buf_len, &off, "groove3", "Groove Effects",
                      P_GROOVE_FX, NELEM(P_GROOVE_FX), KN_GROOVE_FX);
    } else {
        ui_puts(buf, buf_len, &off, ",");
        ui_emit_level(buf, buf_len, &off, "groove1", "Groove 1",
                      P_GROOVE1, NELEM(P_GROOVE1), KN_GROOVE1);
        if (inst && inst->model == MODEL_GEN) {
            ui_puts(buf, buf_len, &off, ",");
            ui_emit_level(buf, buf_len, &off, "groove2", "Groove 2",
                          P_GROOVE2, NELEM(P_GROOVE2), KN_GROOVE2);
        } else if (inst && inst->groove.type == GROOVE_TYPE_TAPS) {
            ui_puts(buf, buf_len, &off, ",");
            ui_emit_level(buf, buf_len, &off, "groove2", "Groove Effects",
                          P_GROOVE_FX, NELEM(P_GROOVE_FX), KN_GROOVE_FX);
        }
    }

    /* Performer chain page (Phase D) — always present. */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "perf1", "Performer", P_PERF, NELEM(P_PERF), KN_PERF);

    ui_puts(buf, buf_len, &off, UI_CLOSE);
    buf[off] = '\0';
    return off;
}
