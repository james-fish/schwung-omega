/* params.c — Central raw-value parameter cache tables + helpers (B1, UIX-01/04). */
#include "params.h"

#include <string.h>

/* Bind the enum counts to the omega.h cache dimensions — they can never drift
 * (the bohm_instance cache arrays are sized by the OMEGA_*_COUNT macros). */
_Static_assert(PKI_COUNT == OMEGA_PKI_COUNT, "PKI_COUNT vs OMEGA_PKI_COUNT drift");
_Static_assert(GKI_COUNT == OMEGA_GKI_COUNT, "GKI_COUNT vs OMEGA_GKI_COUNT drift");

/* ---- Default raw values (schema `default`) -------------------------------
 * Mid (0.5) for continuous kick params so a fresh model is musical, matching
 * the create-time prime; fx_type defaults to 0 (Diode). B2 tunes per-model
 * voicing defaults — this only establishes the readback baseline (UIX-01). */
const float g_kick_defaults[PKI_COUNT] = {
    /* shared. PITCH is a Hz value now (VOICE-01): ~50 Hz techno pocket default. */
    /* CURVE defaults HIGH (0.8) for every model — punchy 808↔909 pitch sweep out
     * of the box (iter-2 feedback: "all models' curve should default ~.75–.8"). */
    [PKI_PITCH]=50.0f, [PKI_LENGTH]=0.5f, [PKI_SUSTAIN]=0.5f, [PKI_CURVE]=0.8f,
    [PKI_ATTACK]=0.5f, [PKI_TRS_DEC]=0.5f, [PKI_TRS_TNE]=0.5f, [PKI_COLOR]=0.5f,
    [PKI_FX_TYPE]=0.0f, [PKI_FX_AMT]=0.0f,
    [PKI_FX_TONE]=0.5f, [PKI_FILTER_ROUTE]=2.0f,   /* tone centered; route=Both
     * (matches the current whole-voice COLOR; Synth/Transient split is a per-
     * model follow-up, see docs/VOICING_AUDIT_v1_1.md). */
    /* FM2 — lower default ratio/index: v=0.5 was piercing/shrill (VOICE-06).
     * 0.22 -> ratio ~2.15, 0.30 -> index ~2.4: warmer, punchy, clean tail. */
    /* FM2 (first model loaded): a clean punchy kick at defaults — low FM index +
     * ratio (iter-2: findex .05, fmratio .1). */
    [PKI_FM_RATIO]=0.10f, [PKI_FM_INDEX]=0.05f, [PKI_OP2_WAVE]=0.35f,
    /* FM4: op params default LOW (iter-2: opratio/opindex .05, amp/fb gentle) —
     * the mapping is also tamed in fm4.c so mid settings aren't harsh noise. */
    [PKI_FM4_ALGO]=0.0f, [PKI_FM4_OPRATIO]=0.05f, [PKI_FM4_OPINDEX]=0.05f,
    [PKI_FM4_OPAMP]=0.1f, [PKI_FM4_FEEDBACK]=0.1f, [PKI_FM4_ALGO2]=0.5f,
    /* WTR — raise WAVE + BODY PITCH defaults: v=0.5 sounded like just the
     * transient (VOICE-06). More body wave + body pitch presence at default. */
    [PKI_WTR_WAVE]=0.6f, [PKI_WTR_BODYPITCH]=0.25f, [PKI_WTR_TRANSDEC]=0.5f,
    [PKI_WTR_TRANSCOL]=0.5f,
    /* PHY */
    [PKI_PHY_BEATER]=0.5f, [PKI_PHY_SHELL]=0.5f, [PKI_PHY_HEADTENS]=0.5f,
    [PKI_PHY_DAMPING]=0.5f,
    /* HRD */
    [PKI_HRD_SAMPLE]=0.5f, [PKI_HRD_MIX]=0.5f, [PKI_HRD_DRIVE]=0.2f,
    [PKI_HRD_CRUSH]=0.0f,
    /* DIG */
    [PKI_DIG_WAVEIDX]=0.5f, [PKI_DIG_SAMPLE]=0.5f, [PKI_DIG_BITDEPTH]=0.5f,
    [PKI_DIG_PITCHENV]=0.5f,
    /* TRS */
    [PKI_TRS_TONE]=0.5f, [PKI_TRS_TDEC]=0.5f, [PKI_TRS_WTCOL]=0.5f,
    [PKI_TRS_CURVE]=0.5f,
    /* ANA — stronger default SUB LEVEL so the 808 boom is obvious (VOICE-06). */
    [PKI_ANA_MORPH]=0.5f, [PKI_ANA_SUBLVL]=0.7f, [PKI_ANA_SUBDEC]=0.6f,
    [PKI_ANA_SAMPLE]=0.5f,
    /* USR */
    [PKI_USR_SAMPLE]=0.0f, [PKI_USR_WTMORPH]=0.5f, [PKI_USR_LAYERVOL]=0.5f,
    [PKI_USR_PITCHENV]=0.5f,
    /* GEN */
    [PKI_GEN_SEED]=0.5f, [PKI_GEN_SCALE]=0.0f, [PKI_GEN_DENSITY]=0.5f,
    [PKI_GEN_SEQLEN]=0.5f, [PKI_GEN_LPFFREQ]=0.5f, [PKI_GEN_LPFPOLE]=0.0f,
};

/* Globals: master starts full, groove seeded to groove_init's normalized
 * middles (grv_vol silent opt-in, taps 0.6, color ~8 kHz -> ~0.44). */
const float g_global_defaults[GKI_COUNT] = {
    [GKI_MASTER_VOL]=1.0f, [GKI_MODEL]=0.0f, [GKI_GRV_TYPE]=0.0f,  /* TAPS */
    /* GRV_VOL default 0: the groove (taps + gen) is an opt-in performance voice.
     * groove_init already seeds g->vol=0 (audibly silent); the UI default must
     * MATCH (0 visually) so the knob isn't shown at 100% while audibly silent and
     * only "activating" once turned (on-device feedback). */
    [GKI_GRV_VOL]=0.0f, [GKI_GRV_LENGTH]=0.5f, [GKI_GRV_COLOR]=0.4375f,
    [GKI_GRV_TAP1]=0.6f, [GKI_GRV_TAP2]=0.6f, [GKI_GRV_TAP3]=0.6f,
    [GKI_GRV_TAP4]=0.6f, [GKI_GRV_MONO]=0.0f,
    /* Groove FX defaults (C1-02): off/neutral so the rumble is unchanged until
     * the user opens them. RVMIX is now the vhr bidirectional reverb knob:
     * CENTER (0.5) = reverb OFF (deadzone), LEFT = pre-smear, RIGHT = post. A
     * bare create therefore sits neutral (rv_pre_amt/rv_post_amt both 0). */
    [GKI_GRV_DRIVE]=0.0f, [GKI_GRV_FILTYPE]=0.0f, [GKI_GRV_LFOSPD]=0.3f, [GKI_GRV_LFOAMT]=0.0f,
    /* Phase 1: RVMIX is now a plain dry/wet (0 = off); ROUTE default 0 = Rumble>Drive>Reverb. */
    [GKI_GRV_RVMIX]=0.0f, [GKI_GRV_RVDECAY]=0.5f, [GKI_GRV_RVTONE]=0.5f, [GKI_GRV_RVTYPE]=0.0f,
    [GKI_GRV_ROUTE]=0.0f,
    /* GEN groove-type defaults (C1-03): musical scale, full 16-step, free-run. */
    [GKI_GRV_GSCALE]=1.0f, [GKI_GRV_GSEED]=0.3f, [GKI_GRV_GSEQLEN]=1.0f, [GKI_GRV_GDENSITY]=0.6f,
    [GKI_GRV_GROTATE]=0.5f, [GKI_GRV_GSWING]=0.5f, [GKI_GRV_GWAVE]=0.0f, [GKI_GRV_GFOLD]=0.0f,  /* GSWING slot = DECAY (medium) */
    [GKI_GRV_GRETRIG]=0.0f,
    /* E3/P1: root — quantized A1 (≈0.542) baseline; unquantized maps log 20..200 Hz,
     * default 0.352 → ~45 Hz sub-bass (Phase 1 GEN-PITCH). Range 12 degrees (0.5). */
    [GKI_GRV_GROOT]=0.18f, [GKI_GRV_GRANGE]=0.5f,   /* unified log root ≈ 45 Hz default */
    /* Performer defaults (Phase D): duck off, DJ filter neutral, clip on. */
    [GKI_DUCK]=0.5f, [GKI_DUCK_REL]=0.4f, [GKI_DUCK_SMT]=0.3f, [GKI_DUCK_BS]=0.5f,
    [GKI_DJ_FILT]=0.5f, [GKI_DJ_RESO]=0.0f, [GKI_CLIP]=0.0f,
};

/* ---- key <-> index ------------------------------------------------------- */
/* One row per kick index; the string is the canonical PK_ macro value. Kept in
 * enum order so pk_kick_key is an O(1) index and pk_kick_index a linear scan. */
static const char *const k_kick_keys[PKI_COUNT] = {
    [PKI_PITCH]=PK_PITCH, [PKI_LENGTH]=PK_LENGTH, [PKI_SUSTAIN]=PK_SUSTAIN,
    [PKI_CURVE]=PK_CURVE, [PKI_ATTACK]=PK_ATTACK, [PKI_TRS_DEC]=PK_TRS_DEC,
    [PKI_TRS_TNE]=PK_TRS_TNE, [PKI_COLOR]=PK_COLOR, [PKI_FX_TYPE]=PK_FX_TYPE,
    [PKI_FX_AMT]=PK_FX_AMT, [PKI_FX_TONE]=PK_FX_TONE,
    [PKI_FILTER_ROUTE]=PK_FILTER_ROUTE,
    [PKI_FM_RATIO]=PK_FM_RATIO, [PKI_FM_INDEX]=PK_FM_INDEX,
    [PKI_OP2_WAVE]=PK_OP2_WAVE,
    [PKI_FM4_ALGO]=PK_FM4_ALGO, [PKI_FM4_OPRATIO]=PK_FM4_OPRATIO,
    [PKI_FM4_OPINDEX]=PK_FM4_OPINDEX, [PKI_FM4_OPAMP]=PK_FM4_OPAMP,
    [PKI_FM4_FEEDBACK]=PK_FM4_FEEDBACK, [PKI_FM4_ALGO2]=PK_FM4_ALGO2,
    [PKI_WTR_WAVE]=PK_WTR_WAVE, [PKI_WTR_BODYPITCH]=PK_WTR_BODYPITCH,
    [PKI_WTR_TRANSDEC]=PK_WTR_TRANSDEC, [PKI_WTR_TRANSCOL]=PK_WTR_TRANSCOL,
    [PKI_PHY_BEATER]=PK_PHY_BEATER, [PKI_PHY_SHELL]=PK_PHY_SHELL,
    [PKI_PHY_HEADTENS]=PK_PHY_HEADTENS, [PKI_PHY_DAMPING]=PK_PHY_DAMPING,
    [PKI_HRD_SAMPLE]=PK_HRD_SAMPLE, [PKI_HRD_MIX]=PK_HRD_MIX,
    [PKI_HRD_DRIVE]=PK_HRD_DRIVE, [PKI_HRD_CRUSH]=PK_HRD_CRUSH,
    [PKI_DIG_WAVEIDX]=PK_DIG_WAVEIDX, [PKI_DIG_SAMPLE]=PK_DIG_SAMPLE,
    [PKI_DIG_BITDEPTH]=PK_DIG_BITDEPTH, [PKI_DIG_PITCHENV]=PK_DIG_PITCHENV,
    [PKI_TRS_TONE]=PK_TRS_TONE, [PKI_TRS_TDEC]=PK_TRS_TDEC,
    [PKI_TRS_WTCOL]=PK_TRS_WTCOL, [PKI_TRS_CURVE]=PK_TRS_CURVE,
    [PKI_ANA_MORPH]=PK_ANA_MORPH, [PKI_ANA_SUBLVL]=PK_ANA_SUBLVL,
    [PKI_ANA_SUBDEC]=PK_ANA_SUBDEC, [PKI_ANA_SAMPLE]=PK_ANA_SAMPLE,
    [PKI_USR_SAMPLE]=PK_USR_SAMPLE, [PKI_USR_WTMORPH]=PK_USR_WTMORPH,
    [PKI_USR_LAYERVOL]=PK_USR_LAYERVOL, [PKI_USR_PITCHENV]=PK_USR_PITCHENV,
    [PKI_GEN_SEED]=PK_GEN_SEED, [PKI_GEN_SCALE]=PK_GEN_SCALE,
    [PKI_GEN_DENSITY]=PK_GEN_DENSITY, [PKI_GEN_SEQLEN]=PK_GEN_SEQLEN,
    [PKI_GEN_LPFFREQ]=PK_GEN_LPFFREQ, [PKI_GEN_LPFPOLE]=PK_GEN_LPFPOLE,
};

static const char *const k_global_keys[GKI_COUNT] = {
    [GKI_MASTER_VOL]=PK_MASTER_VOL, [GKI_MODEL]=PK_MODEL, [GKI_GRV_TYPE]=PK_GRV_TYPE,
    [GKI_GRV_VOL]=PK_GRV_VOL, [GKI_GRV_LENGTH]=PK_GRV_LENGTH,
    [GKI_GRV_COLOR]=PK_GRV_COLOR, [GKI_GRV_TAP1]=PK_GRV_TAP1,
    [GKI_GRV_TAP2]=PK_GRV_TAP2, [GKI_GRV_TAP3]=PK_GRV_TAP3,
    [GKI_GRV_TAP4]=PK_GRV_TAP4, [GKI_GRV_MONO]=PK_GRV_MONO,
    [GKI_GRV_DRIVE]=PK_GRV_DRIVE, [GKI_GRV_FILTYPE]=PK_GRV_FILTYPE,
    [GKI_GRV_LFOSPD]=PK_GRV_LFOSPD, [GKI_GRV_LFOAMT]=PK_GRV_LFOAMT,
    [GKI_GRV_RVMIX]=PK_GRV_RVMIX, [GKI_GRV_RVDECAY]=PK_GRV_RVDECAY,
    [GKI_GRV_RVTONE]=PK_GRV_RVTONE, [GKI_GRV_RVTYPE]=PK_GRV_RVTYPE,
    [GKI_GRV_GSCALE]=PK_GRV_GSCALE, [GKI_GRV_GSEED]=PK_GRV_GSEED,
    [GKI_GRV_GSEQLEN]=PK_GRV_GSEQLEN, [GKI_GRV_GDENSITY]=PK_GRV_GDENSITY,
    [GKI_GRV_GROTATE]=PK_GRV_GROTATE, [GKI_GRV_GSWING]=PK_GRV_GSWING,
    [GKI_GRV_GWAVE]=PK_GRV_GWAVE, [GKI_GRV_GFOLD]=PK_GRV_GFOLD,
    [GKI_GRV_GRETRIG]=PK_GRV_GRETRIG,
    [GKI_GRV_GROOT]=PK_GRV_GROOT, [GKI_GRV_GRANGE]=PK_GRV_GRANGE,
    [GKI_DUCK]=PK_DUCK, [GKI_DUCK_REL]=PK_DUCK_REL, [GKI_DUCK_SMT]=PK_DUCK_SMT,
    [GKI_DUCK_BS]=PK_DUCK_BS, [GKI_DJ_FILT]=PK_DJ_FILT, [GKI_DJ_RESO]=PK_DJ_RESO,
    [GKI_CLIP]=PK_CLIP,
    [GKI_GRV_ROUTE]=PK_GRV_ROUTE,
};

int pk_kick_index(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < PKI_COUNT; i++)
        if (k_kick_keys[i] && strcmp(key, k_kick_keys[i]) == 0) return i;
    return -1;
}

int pk_global_index(const char *key) {
    if (!key) return -1;
    for (int i = 0; i < GKI_COUNT; i++)
        if (k_global_keys[i] && strcmp(key, k_global_keys[i]) == 0) return i;
    return -1;
}

const char *pk_kick_key(int idx) {
    if (idx < 0 || idx >= PKI_COUNT) return NULL;
    return k_kick_keys[idx];
}

const char *pk_global_key(int idx) {
    if (idx < 0 || idx >= GKI_COUNT) return NULL;
    return k_global_keys[idx];
}

/* ---- Locale-independent decimal formatter -------------------------------- */
/* Writes `v` with exactly `decimals` fractional digits. Rounds half-up on the
 * magnitude. No libc %f (which honors LC_NUMERIC and could emit ','). */
int pk_format_value(float v, int decimals, char *buf, int buf_len) {
    if (!buf || buf_len <= 0) return 0;
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;

    int off = 0;
    int neg = (v < 0.0f);
    if (neg) v = -v;

    /* Scale, round half-up, split into integer + fractional strings. */
    float scale = 1.0f;
    for (int i = 0; i < decimals; i++) scale *= 10.0f;
    /* Add 0.5 for round-to-nearest on the last kept digit. */
    double scaled = (double)v * (double)scale + 0.5;
    unsigned long long total = (unsigned long long)scaled;   /* truncate */
    unsigned long long divisor = 1ull;
    for (int i = 0; i < decimals; i++) divisor *= 10ull;
    unsigned long long ip = total / divisor;
    unsigned long long fp = total % divisor;

    char tmp[32];
    int t = 0;

    /* Integer part digits (reversed), at least one '0'. */
    if (ip == 0) { tmp[t++] = '0'; }
    else { while (ip > 0 && t < (int)sizeof tmp) { tmp[t++] = (char)('0' + ip % 10); ip /= 10; } }

    if (neg && off < buf_len - 1) buf[off++] = '-';
    for (int i = t - 1; i >= 0 && off < buf_len - 1; i--) buf[off++] = tmp[i];

    if (decimals > 0 && off < buf_len - 1) {
        buf[off++] = '.';
        /* Fractional part is fp, zero-padded to `decimals` digits. */
        unsigned long long place = divisor / 10ull;
        for (int i = 0; i < decimals && off < buf_len - 1; i++) {
            unsigned long long d = place > 0 ? (fp / place) % 10ull : 0ull;
            buf[off++] = (char)('0' + d);
            place /= 10ull;
        }
    }
    buf[off] = '\0';
    return off;
}
