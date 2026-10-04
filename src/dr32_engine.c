/* dr32_engine.c — Omega's kick models, offered to DR32 as synth engines.
 *
 * DR32 (the 32-pad drum rack) lets any pad play a synth engine instead of a
 * sample, and loads engines other modules bring: it looks for a
 * `dr32_engine.so` in its sibling module folders (src/dr32_engine_api.h is
 * DR32's contract, vendored). This file is that plugin. It is built as its own
 * shared object beside dsp.so and shares the model sources with it; nothing in
 * the module proper changes, and Omega runs exactly as before without DR32.
 *
 * WHAT CROSSES OVER. Eight kick models, one DR32 engine each, with Omega's own
 * knobs: the shared Kick page, the model's own page, and the post-kick FX
 * (type, amount, tone). One instance is one pad's voice.
 *
 * WHAT STAYS BEHIND. The groove rumble, the generative sequencer (GEN), the
 * duck, the DJ filter and the comp/drive glue are Omega's master section; DR32
 * has its own mix, sends and buses per pad. USR needs the user's files and is
 * left out for now.
 *
 * The models are called through their own vtables and are not modified. They
 * read only `model_state`, so each pad's bohm_instance is the model's state
 * and nothing else: the groove rings and the sample bank in it are never
 * touched (and, being calloc'd, never resident).
 */
#include "dr32_engine_api.h"
#include "omega.h"
#include "params.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- parameters --------------------------------------------------------- */
enum { K_HZ, K_PCT, K_ENUM, K_TONE };      /* how a display value reaches the model */

typedef struct { const char *key; int kind; } om_key;

/* The Kick page: every model has these, in Omega's order. */
#define KICK_PARAMS \
    { "pitch",   "Pitch",        "PITCH", 30, 200, 50, 1, "hz", "Kick", NULL }, \
    { "length",  "Length",       "LEN",    0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "curve",   "Curve",        "CURVE",  0, 100, 80, 1, "%",  "Kick", NULL }, \
    { "attack",  "Attack",       "ATK",    0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "trs_dec", "Click Decay",  "TRDEC",  0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "trs_tne", "Click Tone",   "TRTNE",  0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "color",   "Filter",       "FILT",   0, 100, 50, 1, "%",  "Kick", NULL }
#define KICK_KEYS \
    { PK_PITCH, K_HZ }, { PK_LENGTH, K_PCT }, { PK_CURVE, K_PCT }, { PK_ATTACK, K_PCT }, \
    { PK_TRS_DEC, K_PCT }, { PK_TRS_TNE, K_PCT }, { PK_COLOR, K_PCT }
#define KICK_VALUES 50, 50, 80, 50, 50, 50, 50
/* WTR and TRS have a transient-decay knob of their own that writes the SAME
 * field as the Kick page's Click Decay, so there the shared one is left off:
 * two knobs for one value, where the last one turned wins, is one too many. */
#define KICK_PARAMS_OWN_DECAY \
    { "pitch",   "Pitch",        "PITCH", 30, 200, 50, 1, "hz", "Kick", NULL }, \
    { "length",  "Length",       "LEN",    0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "curve",   "Curve",        "CURVE",  0, 100, 80, 1, "%",  "Kick", NULL }, \
    { "attack",  "Attack",       "ATK",    0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "trs_tne", "Click Tone",   "TRTNE",  0, 100, 50, 1, "%",  "Kick", NULL }, \
    { "color",   "Filter",       "FILT",   0, 100, 50, 1, "%",  "Kick", NULL }
#define KICK_KEYS_OWN_DECAY \
    { PK_PITCH, K_HZ }, { PK_LENGTH, K_PCT }, { PK_CURVE, K_PCT }, { PK_ATTACK, K_PCT }, \
    { PK_TRS_TNE, K_PCT }, { PK_COLOR, K_PCT }
#define KICK_VALUES_OWN_DECAY 50, 50, 80, 50, 50, 50

/* The FX page: the post-kick stage. FX TONE is applied here (it lives in
 * Omega's render_block, not in the models). */
#define FX_PARAMS \
    { "fx_type", "FX Type",   "FXTYP", 0, 4, 0, 1, NULL, "FX", "Diode|Clip|SAT|Fold|Crush" }, \
    { "fx_amt",  "FX Amount", "FXAMT", 0, 100, 0, 1, "%", "FX", NULL }, \
    { "fx_tone", "FX Tone",   "FXTNE", 0, 100, 50, 1, "%", "FX", NULL }
#define FX_KEYS { PK_FX_TYPE, K_ENUM }, { PK_FX_AMT, K_PCT }, { PK_FX_TONE, K_TONE }
#define FX_VALUES 0, 0, 50
#define N_FX 3

/* One model's own knob: a 0..100% of Omega's raw 0..1. */
#define P(key, name, shrt, def, page) { key, name, shrt, 0, 100, def, 1, "%", page, NULL }

/* A model's own CHOICE. The models read these as an index (their set_param
 * rounds the raw value), exactly as Omega's own pages offer them. */
#define E(key, name, shrt, count, def, page, options) { key, name, shrt, 0, (count) - 1, def, 1, NULL, page, options }

#define ENGINE(id, ...) \
    static const dr32x_param id##_params[] = { KICK_PARAMS, __VA_ARGS__, FX_PARAMS };
#define KEYS(id, ...) \
    static const om_key id##_keys[] = { KICK_KEYS, __VA_ARGS__, FX_KEYS };
#define VALUES(id, ...) \
    static const float id##_values[] = { KICK_VALUES, __VA_ARGS__, FX_VALUES };
/* ...and the same for a model with its own transient decay (see above). */
#define ENGINE_OWN_DECAY(id, ...) \
    static const dr32x_param id##_params[] = { KICK_PARAMS_OWN_DECAY, __VA_ARGS__, FX_PARAMS };
#define KEYS_OWN_DECAY(id, ...) \
    static const om_key id##_keys[] = { KICK_KEYS_OWN_DECAY, __VA_ARGS__, FX_KEYS };
#define VALUES_OWN_DECAY(id, ...) \
    static const float id##_values[] = { KICK_VALUES_OWN_DECAY, __VA_ARGS__, FX_VALUES };

ENGINE(fm2, P("ratio", "FM Ratio", "RATIO", 10, "FM"), P("index", "FM Index", "INDEX", 5, "FM"),
            E("op2_wave", "Op2 Wave", "WAVE", 3, 0, "FM", "Sine|Fold|Tri"))
KEYS(fm2, { PK_FM_RATIO, K_PCT }, { PK_FM_INDEX, K_PCT }, { PK_OP2_WAVE, K_ENUM })
VALUES(fm2, 10, 5, 0)

ENGINE(fm4, E("algo", "Algorithm", "ALGO", 4, 0, "FM", "Chain|Stacks|Bright|Hollow"), P("op_ratio", "Op Ratio", "RATIO", 5, "FM"),
            P("op_index", "Op Index", "INDEX", 5, "FM"), P("op_amp", "Op Amp", "AMP", 10, "FM"),
            P("feedback", "Feedback", "FDBK", 10, "FM"), P("algo2", "Algorithm 2", "ALGO2", 50, "FM"))
KEYS(fm4, { PK_FM4_ALGO, K_ENUM }, { PK_FM4_OPRATIO, K_PCT }, { PK_FM4_OPINDEX, K_PCT },
          { PK_FM4_OPAMP, K_PCT }, { PK_FM4_FEEDBACK, K_PCT }, { PK_FM4_ALGO2, K_PCT })
VALUES(fm4, 0, 5, 5, 10, 10, 50)

ENGINE_OWN_DECAY(wtr, P("wave", "Wave", "WAVE", 60, "Wave"), P("body_pitch", "Body Pitch", "BODY", 25, "Wave"),
            P("trans_dec", "Trans Decay", "TRDEC", 50, "Wave"), P("trans_col", "Trans Color", "TRCOL", 50, "Wave"))
KEYS_OWN_DECAY(wtr, { PK_WTR_WAVE, K_PCT }, { PK_WTR_BODYPITCH, K_PCT }, { PK_WTR_TRANSDEC, K_PCT }, { PK_WTR_TRANSCOL, K_PCT })
VALUES_OWN_DECAY(wtr, 60, 25, 50, 50)

ENGINE(phy, P("beater", "Beater", "BEATR", 50, "Drum"), P("shell", "Shell", "SHELL", 50, "Drum"),
            P("head_tens", "Head Tension", "HEAD", 50, "Drum"), P("damping", "Damping", "DAMP", 50, "Drum"))
KEYS(phy, { PK_PHY_BEATER, K_PCT }, { PK_PHY_SHELL, K_PCT }, { PK_PHY_HEADTENS, K_PCT }, { PK_PHY_DAMPING, K_PCT })
VALUES(phy, 50, 50, 50, 50)

ENGINE(hrd, E("sample", "Sample Layer", "SMPL", 3, 1, "Hard", "Saw|Square|Digital"), P("mix", "Layer Mix", "MIX", 50, "Hard"),
            P("drive", "Drive", "DRIVE", 20, "Hard"), P("crush", "Crush", "CRUSH", 0, "Hard"))
KEYS(hrd, { PK_HRD_SAMPLE, K_ENUM }, { PK_HRD_MIX, K_PCT }, { PK_HRD_DRIVE, K_PCT }, { PK_HRD_CRUSH, K_PCT })
VALUES(hrd, 1, 50, 20, 0)

ENGINE(dig, E("wave_idx", "Wave", "WAVE", 3, 1, "Digital", "Saw|Square|Digital"), P("sample", "Sample", "SMPL", 50, "Digital"),
            P("bit_depth", "Bit Depth", "BITS", 50, "Digital"), P("pitch_env", "Pitch Env", "PENV", 50, "Digital"))
KEYS(dig, { PK_DIG_WAVEIDX, K_ENUM }, { PK_DIG_SAMPLE, K_PCT }, { PK_DIG_BITDEPTH, K_PCT }, { PK_DIG_PITCHENV, K_PCT })
VALUES(dig, 1, 50, 50, 50)

ENGINE_OWN_DECAY(trs, P("tone", "Trans Tone", "TONE", 50, "Transient"), P("tdec", "Trans Decay", "TDEC", 50, "Transient"),
            P("wt_col", "Wave Color", "WTCOL", 50, "Transient"), P("tcurve", "Trans Curve", "TCRV", 50, "Transient"),
            { "filter_route", "Filter Route", "ROUTE", 0, 2, 2, 1, NULL, "Transient", "Synth|Transient|Both" })
KEYS_OWN_DECAY(trs, { PK_TRS_TONE, K_PCT }, { PK_TRS_TDEC, K_PCT }, { PK_TRS_WTCOL, K_PCT }, { PK_TRS_CURVE, K_PCT },
          { PK_FILTER_ROUTE, K_ENUM })
VALUES_OWN_DECAY(trs, 50, 50, 50, 50, 2)

ENGINE(ana, P("morph", "Morph", "MORPH", 50, "Analog"), P("sub_lvl", "Sub Level", "SUB", 70, "Analog"),
            P("sub_dec", "Sub Decay", "SUBDC", 60, "Analog"), P("sample", "Sample", "SMPL", 50, "Analog"))
KEYS(ana, { PK_ANA_MORPH, K_PCT }, { PK_ANA_SUBLVL, K_PCT }, { PK_ANA_SUBDEC, K_PCT }, { PK_ANA_SAMPLE, K_PCT })
VALUES(ana, 50, 70, 60, 50)

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const struct om_model {
    const kick_model_vtable_t *vt;
    const om_key *keys;
    int nparams;
} MODELS[] = {
    { &g_fm2_vtable, fm2_keys, COUNT(fm2_keys) }, { &g_fm4_vtable, fm4_keys, COUNT(fm4_keys) },
    { &g_wtr_vtable, wtr_keys, COUNT(wtr_keys) }, { &g_phy_vtable, phy_keys, COUNT(phy_keys) },
    { &g_hrd_vtable, hrd_keys, COUNT(hrd_keys) }, { &g_dig_vtable, dig_keys, COUNT(dig_keys) },
    { &g_trs_vtable, trs_keys, COUNT(trs_keys) }, { &g_ana_vtable, ana_keys, COUNT(ana_keys) },
};

/* ---- one pad's voice ---------------------------------------------------- */
typedef struct {
    const struct om_model *m;
    float pitch_hz;         /* the Pitch knob; a hit plays it shifted by the pad's tune */
    float fx_tone;          /* 0..1, 0.5 neutral */
    float lp_l, lp_r;       /* FX TONE split state */
    bohm_instance_t inst;   /* LAST, and only its head is allocated: see OM_VOICE_BYTES */
} om_voice;

/* The models read and write `model_state` and nothing after it, so a pad's
 * voice stops there: the groove rings, the user buffers and the sample bank
 * that follow are 2 MB a pad that no kick model touches. */
#define OM_VOICE_BYTES (offsetof(om_voice, inst) + offsetof(bohm_instance_t, usr_wavetable))
_Static_assert(offsetof(bohm_instance_t, model_state) + sizeof(((bohm_instance_t *)0)->model_state)
               <= offsetof(bohm_instance_t, usr_wavetable),
               "model_state must end before the first field a pad's voice leaves out");

static void om_write(om_voice *v, const char *key, float raw) {
    char buf[24];
    pk_format_value(raw, 4, buf, (int)sizeof buf);
    v->m->vt->set_param(&v->inst, key, buf);
}

static void om_set(void *e, int idx, float display) {
    om_voice *v = (om_voice *)e;
    if (idx < 0 || idx >= v->m->nparams) return;
    const om_key *k = &v->m->keys[idx];
    switch (k->kind) {
        case K_HZ:   v->pitch_hz = display; om_write(v, k->key, display); break;
        case K_PCT:  om_write(v, k->key, display * 0.01f); break;
        case K_ENUM: om_write(v, k->key, (float)(int)(display + 0.5f)); break;
        case K_TONE: v->fx_tone = display * 0.01f; break;
    }
}

static void *om_create_model(int which, int sample_rate) {
    (void)sample_rate;                                  /* OMEGA_SR; the entry refuses any other */
    om_voice *v = (om_voice *)calloc(1, OM_VOICE_BYTES);
    if (!v) return NULL;
    v->m = &MODELS[which];
    v->fx_tone = 0.5f;
    v->pitch_hz = 50.0f;
    om_write(v, PK_SUSTAIN, 0.5f);      /* Omega's default; it has no knob for it */
    return v;
}
#define CREATE(n) static void *om_create_##n(int sr) { return om_create_model(n, sr); }
CREATE(0) CREATE(1) CREATE(2) CREATE(3) CREATE(4) CREATE(5) CREATE(6) CREATE(7)

static void om_destroy(void *e) { free(e); }

static void om_note_on(void *e, float vel01, float tune_st) {
    om_voice *v = (om_voice *)e;
    /* The pad's transpose and detune move the kick's pitch; the knob keeps its value. */
    if (tune_st != 0.0f) om_write(v, PK_PITCH, v->pitch_hz * powf(2.0f, tune_st / 12.0f));
    else om_write(v, PK_PITCH, v->pitch_hz);
    int vel = (int)(vel01 * 127.0f + 0.5f);
    v->m->vt->trigger(&v->inst, 60, vel < 1 ? 1 : vel > 127 ? 127 : vel);
}

/* DR32 ends a voice that has gone quiet and fades a choked pad itself, so
 * there is no gate and no choke here: render says the voice is still going. */
static int om_render(void *e, float *out, int n) {
    om_voice *v = (om_voice *)e;
    float l[OMEGA_MAX_BLOCK], r[OMEGA_MAX_BLOCK];
    float hi_gain = 2.0f * v->fx_tone;
    for (int done = 0; done < n;) {
        int c = n - done > OMEGA_MAX_BLOCK ? OMEGA_MAX_BLOCK : n - done;
        v->m->vt->render(&v->inst, l, r, c);
        for (int i = 0; i < c; i++) {
            /* Omega's FX TONE: a fixed one-pole split, highs scaled. This is
             * dsp.c's render_block stage, which the models do not contain. */
            v->lp_l += 0.157f * (l[i] - v->lp_l);
            v->lp_r += 0.157f * (r[i] - v->lp_r);
            out[done + i] = 0.5f * ((v->lp_l + (l[i] - v->lp_l) * hi_gain) + (v->lp_r + (r[i] - v->lp_r) * hi_gain));
        }
        done += c;
    }
    return DR32X_RENDER_ALIVE;
}

/* ---- what DR32 is told -------------------------------------------------- */
#define ENG(slug, name, id, n) \
    { slug, name, COUNT(id##_params), id##_params, om_create_##n, om_destroy, om_set, om_note_on, NULL, om_render }
static const dr32x_engine ENGINES[] = {
    ENG("fm2", "Omega FM2", fm2, 0), ENG("fm4", "Omega FM4", fm4, 1),
    ENG("wtr", "Omega WTR", wtr, 2), ENG("phy", "Omega PHY", phy, 3),
    ENG("hrd", "Omega HRD", hrd, 4), ENG("dig", "Omega DIG", dig, 5),
    ENG("trs", "Omega TRS", trs, 6), ENG("ana", "Omega ANA", ana, 7),
};

/* One model per engine, at Omega's own defaults. */
static const dr32x_model KICKS[] = {
    { "fm2", "FM2 Kick", 0, fm2_values, 0.0f, 0.0f }, { "fm4", "FM4 Kick", 1, fm4_values, 0.0f, 0.0f },
    { "wtr", "WTR Kick", 2, wtr_values, 0.0f, 0.0f }, { "phy", "PHY Kick", 3, phy_values, 0.0f, 0.0f },
    { "hrd", "HRD Kick", 4, hrd_values, 0.0f, 0.0f }, { "dig", "DIG Kick", 5, dig_values, 0.0f, 0.0f },
    { "trs", "TRS Kick", 6, trs_values, 0.0f, 0.0f }, { "ana", "ANA Kick", 7, ana_values, 0.0f, 0.0f },
};

static const dr32x_plugin PLUGIN = {
    DR32X_API_VERSION, sizeof(dr32x_plugin), "omega", "Omega",
    COUNT(ENGINES), ENGINES, COUNT(KICKS), KICKS,
};

_Static_assert(COUNT(fm2_params) == COUNT(fm2_keys) && COUNT(fm2_keys) == COUNT(fm2_values), "fm2 tables");
_Static_assert(COUNT(fm4_params) == COUNT(fm4_keys) && COUNT(fm4_keys) == COUNT(fm4_values), "fm4 tables");
_Static_assert(COUNT(wtr_params) == COUNT(wtr_keys) && COUNT(wtr_keys) == COUNT(wtr_values), "wtr tables");
_Static_assert(COUNT(phy_params) == COUNT(phy_keys) && COUNT(phy_keys) == COUNT(phy_values), "phy tables");
_Static_assert(COUNT(hrd_params) == COUNT(hrd_keys) && COUNT(hrd_keys) == COUNT(hrd_values), "hrd tables");
_Static_assert(COUNT(dig_params) == COUNT(dig_keys) && COUNT(dig_keys) == COUNT(dig_values), "dig tables");
_Static_assert(COUNT(trs_params) == COUNT(trs_keys) && COUNT(trs_keys) == COUNT(trs_values), "trs tables");
_Static_assert(COUNT(ana_params) == COUNT(ana_keys) && COUNT(ana_keys) == COUNT(ana_values), "ana tables");

__attribute__((visibility("default")))
const dr32x_plugin *dr32_engine_plugin(const dr32x_host *host) {
    /* The models are fixed at 44.1 kHz (OMEGA_SR). */
    if (!host || host->api_version < DR32X_API_VERSION || host->sample_rate != (int)OMEGA_SR) return NULL;
    return &PLUGIN;
}
