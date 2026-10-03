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
    char obj[320];
    if (p->type == UP_ENUM) {
        pk_format_value(ui_default_for(p->key), 0, defbuf, (int)sizeof defbuf);
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
        float norm = ui_default_for(p->key);
        float di   = mnv + norm * (mxv - mnv);
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
static const char OPT_MODEL[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\"]";
static const char OPT_FX[]    = "[\"Diode\",\"Clip\",\"SAT\",\"Fold\",\"Crush\"]";
static const char OPT_MONO[]  = "[\"Stereo\",\"Mono\"]";
static const char OPT_POLE[]  = "[\"2-pole\",\"4-pole\"]";
static const char OPT_ROUTE[] = "[\"Synth\",\"Transient\",\"Both\"]";
static const char OPT_GRVTYPE[] = "[\"Taps\",\"Gen\"]";
static const char OPT_GFILT[]   = "[\"LP\",\"HP\",\"Off\"]";
static const char OPT_RVTYPE[]  = "[\"Room\",\"Hall\",\"Plate\"]";
static const char OPT_SCALE[]   = "[\"Unquantized\",\"Chromatic\",\"Major\",\"Minor\","
    "\"Penta\",\"Dorian\",\"Phrygian\",\"Mixolydian\","
    "\"Hirajoshi\",\"Hungarian\",\"WholeTone\",\"Blues\",\"Diminished\"]";
static const char OPT_GWAVE[]   = "[\"Sine\",\"Tri\",\"Saw\",\"Square\",\"Digital\",\"Analog\"]";
static const char OPT_RETRIG[]  = "[\"None\",\"1 Bar\",\"2 Bar\",\"4 Bar\",\"8 Bar\",\"On Note\"]";
static const char OPT_ONOFF[]   = "[\"Off\",\"On\"]";
/* Phase 1 FX-ROUTE: groove FX order (RUMBLE/DRIVE/REVERB). */
static const char OPT_FXROUTE[] = "[\"Rmbl>Drv>Rev\",\"Rmbl>Rev>Drv\",\"Rev>Rmbl>Drv\",\"Drv>Rmbl>Rev\"]";

/* Performer chain page (Phase D, PERF-05): duck -> DJ filter -> clip. DJ FILT is
 * a bidirectional centered sweep (0.5 = neutral). */
static const uiparam_t P_PERF[] = {
    { PK_MASTER_VOL, "MSTR VOL", "MVOL",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK,       "DUCK",     "DUCK",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_REL,   "DUCK REL", "DKREL", UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_SMT,   "DUCK SLEW","SLEW",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DUCK_BS,    "DUCK FREQ","FREQ",  UP_FLOAT, "%", "0.01", NULL },
    { PK_DJ_FILT,    "DJ FILT",  "DJFLT", UP_FLOAT, "%", "0.01", NULL },
    { PK_DJ_RESO,    "DJ RESO",  "DJRES", UP_FLOAT, "%", "0.01", NULL },
    { PK_CLIP,       "CMPDR",    "CMPDR", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_PERF[] =
    "[\"" PK_MASTER_VOL "\",\"" PK_DUCK "\",\"" PK_DUCK_REL "\",\"" PK_DUCK_SMT
    "\",\"" PK_DUCK_BS "\",\"" PK_DJ_FILT "\",\"" PK_DJ_RESO "\",\"" PK_CLIP "\"]";

/* ---- level tables -------------------------------------------------------- */
static const uiparam_t P_ROOT[] = {
    { PK_MODEL,      "Model",  "MODEL", UP_ENUM,  "",  "0", OPT_MODEL },
    { PK_MASTER_VOL, "Volume", "VOL",   UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_ROOT[] = "[\"" PK_MODEL "\",\"" PK_MASTER_VOL "\"]";

/* Kick Page 1 (B2 reorg, VOICE-04): the fun lives here — PITCH, LENGTH, CURVE
 * (LENGTH now folds in SUSTAIN, VOICE-03) followed by the ACTIVE model's unique
 * params (spliced from its p2_slot_desc). PITCH exposed in Hz (VOICE-01). */
static const uiparam_t P_KICK1[] = {
    { PK_PITCH,   "PITCH",   "PITCH", UP_FLOAT, "Hz", "1",    NULL, "30", "200" },
    { PK_LENGTH,  "LENGTH",  "LEN",   UP_FLOAT, "%",  "0.01", NULL },
    { PK_CURVE,   "CURVE",   "CURVE", UP_FLOAT, "%",  "0.01", NULL },
};

/* Kick Page 2 (B2 reorg, VOICE-04): static shared set — the 3 transients, the
 * body FILTER (=COLOR) with routing, and the post FX (type/amount/tone). TRS TNE
 * shapes the transient; FILTER + ROUTE replace the old separate COLOR (VOICE-03/
 * 05). PITCH is in Hz but PK_PITCH still parses 0..1 internally in Phase B2-01;
 * the Hz domain conversion lands with the per-model voicing pass (B2-02). */
static const uiparam_t P_KICK2[] = {
    { PK_ATTACK,       "ATTACK",   "ATK",   UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_DEC,      "TRS DEC",  "TRSDEC",UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_TNE,      "TRS TNE",  "TRSTNE",UP_FLOAT, "%", "0.01", NULL },
    { PK_COLOR,        "FILTER",   "FILT",  UP_FLOAT, "%", "0.01", NULL },
    { PK_FILTER_ROUTE, "FILT RTE", "RTE",   UP_ENUM,  "",  "0",    OPT_ROUTE },
    { PK_FX_TYPE,      "FX TYPE",  "FXTYPE",UP_ENUM,  "",  "0",    OPT_FX },
    { PK_FX_AMT,       "FX AMT",   "FXAMT", UP_FLOAT, "%", "0.01", NULL },
    { PK_FX_TONE,      "FX TONE",  "FXTONE",UP_FLOAT, "%", "0.01", NULL },
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
static const uiparam_t P_GROOVE1[] = {
    { PK_GRV_VOL,    "VOL",    "VOL",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LENGTH, "LENGTH", "LEN",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_COLOR,  "LPF",    "LPF",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP1,   "TAP1",   "TAP1", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP2,   "TAP2",   "TAP2", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP3,   "TAP3",   "TAP3", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP4,   "TAP4",   "TAP4", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_GROOVE1[] =
    "[\"" PK_GRV_VOL "\",\"" PK_GRV_LENGTH "\",\"" PK_GRV_COLOR
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
    { PK_GRV_DRIVE,   "DRIVE",   "DRIVE", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LFOSPD,  "LFO SPD", "LFOSPD",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LFOAMT,  "LFO AMT", "LFOAMT",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVMIX,   "REVERB",  "REV",   UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVDECAY, "RV DECAY","RVDEC", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_RVTONE,  "RV TONE", "RVTONE",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_ROUTE,   "ROUTE",   "ROUTE", UP_ENUM,  "",  "0",    OPT_FXROUTE },
    { PK_GRV_RVTYPE,  "RV TYPE", "RVTYPE",UP_ENUM,  "",  "0",    OPT_RVTYPE },
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
    { PK_GRV_GSEED,    "SEED",    "SEED",  UP_FLOAT, "",  "0.0079", NULL, "0",   "127" },
    { PK_GRV_GSEQLEN,  "SEQ LEN", "SEQLEN",UP_INT,   "",  "1",      NULL, "1",   "64"  },
    { PK_GRV_GDENSITY, "DENSITY", "DENS",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GROTATE,  "ROTATE",  "ROT",   UP_FLOAT, "",  "0.0156", NULL, "-32", "32"  },
    { PK_GRV_GSWING,   "DECAY",   "DECAY", UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GWAVE,    "WAVE",    "WAVE",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GFOLD,    "FOLD",    "FOLD",  UP_FLOAT, "%", "0.01",   NULL },
    { PK_GRV_GENFILT,  "FILTER",  "FILT",  UP_FLOAT, "%", "0.01",   NULL },  /* GEN's own filter (v0.4) */
};
static const char KN_GROOVE_GEN_SEQ[] =
    "[\"" PK_GRV_GSEED "\",\"" PK_GRV_GSEQLEN "\",\"" PK_GRV_GDENSITY
    "\",\"" PK_GRV_GROTATE "\",\"" PK_GRV_GSWING "\",\"" PK_GRV_GWAVE
    "\",\"" PK_GRV_GFOLD "\",\"" PK_GRV_GENFILT "\"]";

/* (Legacy MODEL_GEN-kick groove2 page removed: GEN is not a selectable kick
 * model — OPT_MODEL is FM2..USR — so that branch was unreachable.) */

/* ---- wrapper fragments --------------------------------------------------- */
static const char UI_OPEN[] =
    "{\"pad_layout\":\"drums\",\"child_index_param\":\"current_pad\",\"levels\":{";
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
    static const uiparam_t p_head[] = {
        { PK_GRV_GENVOL, "GEN VOL","GVOL",  UP_FLOAT, "%","0.01", NULL,      NULL, NULL },
        { PK_GRV_GSCALE, "SCALE",  "SCALE", UP_ENUM,  "", "0",    OPT_SCALE, NULL, NULL },
    };
    static const uiparam_t p_tail[] = {
        { PK_GRV_GRANGE,  "RANGE",  "RANGE",  UP_FLOAT, "", "0.04", NULL, NULL, NULL },
        { PK_GRV_GRETRIG, "RETRIG", "RETRIG", UP_ENUM,  "", "0",    OPT_RETRIG, NULL, NULL },
    };
    (void)inst;
    ui_puts(buf, buf_len, off, "\"gengroove1\":{\"name\":\"Groove Gen\",\"params\":[");
    for (int i = 0; i < (int)(sizeof p_head / sizeof p_head[0]); i++) {
        if (i) ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &p_head[i]);
    }
    /* ROOT (Hz float) — used in Unquantized mode. */
    ui_puts(buf, buf_len, off,
        ",{\"key\":\"" PK_GRV_GROOT "\",\"name\":\"ROOT\",\"short_name\":\"ROOT\","
        "\"type\":\"float\",\"min\":20,\"max\":520,\"default\":45,\"step\":1,\"unit\":\"Hz\"}");
    /* ROOT NOTE (note-name enum) — used in a scale. */
    ui_puts(buf, buf_len, off,
        ",{\"key\":\"" PK_GRV_GROOTNOTE "\",\"name\":\"ROOT NOTE\",\"short_name\":\"ROOTN\","
        "\"type\":\"enum\",\"options\":");
    ui_emit_root_note_options(buf, buf_len, off);
    ui_puts(buf, buf_len, off, ",\"default\":30}");
    for (int i = 0; i < (int)(sizeof p_tail / sizeof p_tail[0]); i++) {
        ui_puts(buf, buf_len, off, ",");
        ui_emit_param(buf, buf_len, off, &p_tail[i]);
    }
    ui_puts(buf, buf_len, off,
        "],\"knobs\":[\""
        PK_GRV_GENVOL "\",\"" PK_GRV_GSCALE "\",\"" PK_GRV_GROOT "\",\""
        PK_GRV_GROOTNOTE "\",\"" PK_GRV_GRANGE "\",\"" PK_GRV_GRETRIG "\"]}");
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
     *   groove1    "Groove Taps"  (TAPS VOL + FIR rumble controls)
     *   gengroove1 "Groove Gen"   (GEN VOL/SCALE/ROOT/ROOT NOTE/RANGE/RETRIG)
     *   genseq     "Gen Seq"      (SEED/SEQLEN/.../GEN FILTER)
     *   groovefx   "Groove Effects" (shared drive/reverb/route) */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "groove1", "Groove Taps",
                  P_GROOVE1, NELEM(P_GROOVE1), KN_GROOVE1, NULL);
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_gen_groove1(buf, buf_len, &off, inst);          /* "gengroove1" "Groove Gen" */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "genseq", "Gen Seq",
                  P_GROOVE_GEN_SEQ, NELEM(P_GROOVE_GEN_SEQ), KN_GROOVE_GEN_SEQ, NULL);
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "groovefx", "Groove Effects",
                  P_GROOVE_FX, NELEM(P_GROOVE_FX), KN_GROOVE_FX, NULL);

    /* Performer chain page (Phase D) — always present, no gate. */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "perf1", "Performer", P_PERF, NELEM(P_PERF), KN_PERF, NULL);

    ui_puts(buf, buf_len, &off, UI_CLOSE);
    buf[off] = '\0';
    return off;
}
