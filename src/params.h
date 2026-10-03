/* params.h — Central raw-value parameter cache (v1.1 Phase B1, UIX-01/04).
 *
 * The kick models store DERIVED values (Hz, ms, filter coefficients) in their
 * opaque model_state; they never keep the raw normalized value the host set. So
 * get_param had nothing to echo back (every knob rendered at 0) and a model
 * switch lost all state. This module adds a single source of truth for the raw
 * value of every parameter, decoupled from each model's DSP state:
 *
 *   - kick_cache[MODEL_COUNT][PKI_COUNT] : every kick param, PER MODEL, so each
 *     model remembers its own knob positions (UIX-04). Model-unique Page-2 keys
 *     are unique strings; storing a full row per model is harmless.
 *   - global_cache[GKI_COUNT]            : master_vol, model, and the 8 groove
 *     keys (global, not per-model).
 *
 * set_param records into the cache (control rate); get_param formats the cached
 * value back out (UIX-01). On a model switch dsp.c replays the target model's
 * cached values so both the sound AND the knob positions are restored.
 *
 * No allocation, no transcendentals — pure table lookup + a locale-independent
 * float formatter. Safe to call from the audio/UI thread.
 */
#ifndef OMEGA_PARAMS_H
#define OMEGA_PARAMS_H

#include "omega.h"

/* Kick parameter indices — shared Page-1 keys first, then each model's Page-2
 * keys. Append-only within a model group is fine; the enum only indexes the
 * cache, it is not part of any ABI. */
typedef enum {
    /* Shared (Page 1 + Page 2 transient/FX/filter) */
    PKI_PITCH = 0, PKI_LENGTH, PKI_SUSTAIN, PKI_CURVE, PKI_ATTACK,
    PKI_TRS_DEC, PKI_TRS_TNE, PKI_COLOR, PKI_FX_TYPE, PKI_FX_AMT,
    PKI_FX_TONE, PKI_FILTER_ROUTE,
    /* FM2 */
    PKI_FM_RATIO, PKI_FM_INDEX, PKI_OP2_WAVE,
    /* FM4 */
    PKI_FM4_ALGO, PKI_FM4_OPRATIO, PKI_FM4_OPINDEX, PKI_FM4_OPAMP,
    PKI_FM4_FEEDBACK, PKI_FM4_ALGO2,
    /* WTR */
    PKI_WTR_WAVE, PKI_WTR_BODYPITCH, PKI_WTR_TRANSDEC, PKI_WTR_TRANSCOL,
    /* PHY */
    PKI_PHY_BEATER, PKI_PHY_SHELL, PKI_PHY_HEADTENS, PKI_PHY_DAMPING,
    /* HRD */
    PKI_HRD_SAMPLE, PKI_HRD_MIX, PKI_HRD_DRIVE, PKI_HRD_CRUSH,
    /* DIG */
    PKI_DIG_WAVEIDX, PKI_DIG_SAMPLE, PKI_DIG_BITDEPTH, PKI_DIG_PITCHENV,
    /* TRS */
    PKI_TRS_TONE, PKI_TRS_TDEC, PKI_TRS_WTCOL, PKI_TRS_CURVE,
    /* ANA */
    PKI_ANA_MORPH, PKI_ANA_SUBLVL, PKI_ANA_SUBDEC, PKI_ANA_SAMPLE,
    /* USR */
    PKI_USR_SAMPLE, PKI_USR_WTMORPH, PKI_USR_LAYERVOL, PKI_USR_PITCHENV,
    /* GEN */
    PKI_GEN_SEED, PKI_GEN_SCALE, PKI_GEN_DENSITY, PKI_GEN_SEQLEN,
    PKI_GEN_LPFFREQ, PKI_GEN_LPFPOLE,
    PKI_COUNT
} pk_kick_index_t;

/* Global (non-per-model) parameter indices. */
typedef enum {
    GKI_MASTER_VOL = 0, GKI_MODEL, GKI_GRV_TYPE,
    GKI_GRV_VOL, GKI_GRV_LENGTH, GKI_GRV_COLOR,
    GKI_GRV_TAP1, GKI_GRV_TAP2, GKI_GRV_TAP3, GKI_GRV_TAP4, GKI_GRV_MONO,
    GKI_GRV_DRIVE, GKI_GRV_FILTYPE, GKI_GRV_LFOSPD, GKI_GRV_LFOAMT,
    GKI_GRV_RVMIX, GKI_GRV_RVDECAY, GKI_GRV_RVTONE, GKI_GRV_RVTYPE,
    GKI_GRV_GSCALE, GKI_GRV_GSEED, GKI_GRV_GSEQLEN, GKI_GRV_GDENSITY,
    GKI_GRV_GROTATE, GKI_GRV_GSWING, GKI_GRV_GWAVE, GKI_GRV_GFOLD, GKI_GRV_GRETRIG,
    GKI_GRV_GROOT, GKI_GRV_GRANGE,
    GKI_DUCK, GKI_DUCK_REL, GKI_DUCK_SMT, GKI_DUCK_BS,
    GKI_DJ_FILT, GKI_DJ_RESO, GKI_CLIP,
    GKI_GRV_ROUTE,                /* Phase 1 FX-ROUTE: groove FX order selector */
    GKI_GRV_GENVOL, GKI_GRV_GROOTNOTE, GKI_GRV_GENFILT,  /* v0.4 dual-voice groove */
    GKI_GRV_HPF, GKI_GRV_GNOTELEN, GKI_GRV_GSWINGAMT, GKI_GRV_GENTAPS,  /* v0.4.1 batch */
    GKI_COUNT
} pk_global_index_t;

/* Default raw value for every cached parameter (schema `default`). */
extern const float g_kick_defaults[PKI_COUNT];
extern const float g_global_defaults[GKI_COUNT];

/* key -> index, or -1 if the key is not in that namespace. */
int pk_kick_index(const char *key);
int pk_global_index(const char *key);

/* index -> canonical key string (for replaying the cache on model switch). */
const char *pk_kick_key(int idx);
const char *pk_global_key(int idx);

/* Locale-independent decimal formatter: writes `v` with `decimals` fractional
 * digits into buf (bounded by buf_len), null-terminated. Returns bytes written
 * (excluding the terminator), or 0 on overflow. Never uses libc %f (locale). */
int pk_format_value(float v, int decimals, char *buf, int buf_len);

#endif /* OMEGA_PARAMS_H */
