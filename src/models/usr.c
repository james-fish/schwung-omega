/* usr.c — USR / XT-88 model: user-loaded WAV + user wavetable (KICK-10).
 *
 * The user-content kick (B-RESEARCH §USR): plays a user one-shot SAMPLE and/or
 * a user single-cycle WAVETABLE loaded OFF the render loop at create_instance
 * (dsp.c usr_load_wav / usr_load_wavetable) into the pre-sized bohm_instance
 * usr_* buffers (Pitfall 4: the big buffer lives in the instance, NOT in
 * model_state[4096]). USR does ZERO file I/O — it only READS the already-loaded
 * buffers here.
 *
 * FALLBACK (D-B02 non-silent default): when no user file was present
 * (inst->usr_loaded == false) USR synthesises a built-in wavetable body from the
 * factory .rodata sine/analog waves via wt_read_bl, pitch-swept with the shared
 * FM2 dual-envelope 808<->909 CURVE blend, so USR is a usable techno kick out of
 * the box with no user content.
 *
 * Page-2 slots (KICK-10): SAMPLE SELECT (sample vs built-in body source),
 * WT MORPH (morph the user/built-in wavetable), LAYER VOL (sample<->wavetable
 * layer mix), PITCH ENV (downward pitch-sweep depth on the wavetable body).
 *
 * Signal flow:
 *   body   = morph( user_wavetable OR built-in wave, WT MORPH ) * amp_env
 *   sample = usr_sample[cursor] * amp_env       (only when a user sample loaded)
 *   s      = lerp(body, sample, LAYER VOL)      (LAYER VOL biases toward sample)
 *   s      = COLOR output LP -> fx_process (KICK-14)
 *
 * All coeff/transcendental work happens in set_param/trigger, never per-sample
 * (no tanf/expf/powf in render). Shares fm2.c/wtr.c structure (Pattern 4).
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>
#include <stdio.h>   /* B3: snprintf for the dynamic SAMPLE SEL enum descriptor */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The instance USR-wavetable geometry must match the primitive geometry so the
 * branch-free guard-sample read (t[WT_LEN]==t[0]) holds for the user table. */
_Static_assert(OMEGA_WT_LEN == WT_LEN, "USR wavetable length matches primitives");
_Static_assert(OMEGA_WT_GUARD == WT_GUARD, "USR wavetable guard matches primitives");

/* ---- USR per-instance state (overlays bohm_instance.model_state) --------- */
/* Only playback cursors + coeffs live here; the large sample/wavetable buffers
 * live in bohm_instance (loaded off-render). */
typedef struct usr_state {
    /* Pitch / body oscillator */
    float f0;                 /* fundamental Hz (from PITCH) */
    float sweep_hz;           /* pitch-env depth in Hz above f0 */
    env_t pitch_env_fast;     /* 909 fast sweep */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 (slow), 1 = 909 (fast) */
    float body_phase;         /* body wavetable phase [0,1) */
    float pitch_env_amt;      /* PITCH ENV: extra downward sweep depth [0,1] */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* User-content playback controls */
    int   sample_idx;         /* SAMPLE SELECT (B3): 0=None, 1..N = bank slot */
    float sample_sel;         /* body-morph fallback when the bank is empty */
    float wt_morph;           /* WT MORPH: morph the wavetable timbre [0,1] */
    float layer_vol;          /* LAYER VOL: body<->sample layer mix [0,1] */
    float sample_pos;         /* user-sample playback cursor (frames, fractional) */

    /* Attack transient (from ATTACK/TRS DEC/TRS TNE) */
    noise_t trs_noise;
    env_t   trs_env;
    float   trs_amp;
    float   trs_dec_ms;
    tpt1_t  trs_tone_lp; float trs_tone_g;
    float   trs_tne_mix;   /* TRS TNE: LP(dark) <-> raw(bright) click blend */

    /* Output COLOR lowpass */
    tpt1_t color_lp;    float color_g;

    /* Post-kick FX chain (KICK-14). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} usr_state;

_Static_assert(sizeof(usr_state) <= 4096, "usr_state fits model_state");

/* ---- Locale-independent float parser (UI-01; copied from fm2.c) ---------- */
static float parse_f(const char *s) {
    if (!s) return 0.0f;
    while (*s == ' ' || *s == '\t') s++;
    float sign = 1.0f;
    if (*s == '+') { s++; }
    else if (*s == '-') { sign = -1.0f; s++; }
    float ip = 0.0f;
    while (*s >= '0' && *s <= '9') { ip = ip * 10.0f + (float)(*s - '0'); s++; }
    float fp = 0.0f, scale = 0.1f;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { fp += (float)(*s - '0') * scale; scale *= 0.1f; s++; }
    }
    return sign * (ip + fp);
}

static inline float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);
}

/* ---- Parameter dispatch (Page 1 + USR Page 2 keys) ----------------------- */
void usr_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    usr_state *u = (usr_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        u->f0 = omega_pitch_hz(val);       /* exp map [35,120] Hz */
        u->sweep_hz = clampf(u->f0 * (1.5f + u->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        u->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        u->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        u->curve = v;
        u->sweep_hz = clampf(u->f0 * (1.5f + u->curve * 7.0f), 0.0f, 1000.0f);
    } else if (strcmp(key, PK_ATTACK) == 0) {
        u->trs_amp = v;
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        u->trs_dec_ms = 1.0f + v * (30.0f - 1.0f);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* TRS TNE: dark(0) <-> bright(1) click blend (B-04 lesson — blending the
         * LP'd click toward RAW noise shifts brightness while KEEPING the click
         * audible, unlike a pure LP cutoff which attenuates it to inaudibility). */
        float fc = 500.0f + v * (16000.0f - 500.0f);
        u->trs_tone_g = tpt_g_from_hz(fc);
        u->trs_tne_mix = v;
    } else if (strcmp(key, PK_COLOR) == 0) {
        float fc = 200.0f + v * (18000.0f - 200.0f);
        u->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_USR_SAMPLE) == 0) {
        /* SAMPLE SELECT (B3, SMPL-02): a picker into the enumerated sample bank.
         * 0 = None, 1..N = bank slot. When the bank is EMPTY it falls back to an
         * always-audible body morph so the control still shapes the sound (and
         * the voicing battery stays green with no samples installed). */
        if (inst->sample_count > 0) {
            int idx = (int)(parse_f(val) + 0.5f);
            if (idx < 0) idx = 0;
            if (idx > inst->sample_count) idx = inst->sample_count;
            u->sample_idx = idx;
            u->sample_sel = 0.0f;
        } else {
            u->sample_idx = 0;
            u->sample_sel = v;   /* body-morph fallback (no bank) */
        }
    } else if (strcmp(key, PK_USR_WTMORPH) == 0) {
        /* WT MORPH: morph the wavetable timbre — for a user table it lerps
         * toward a folded/brighter variant; for the built-in fallback it selects
         * across the factory waves. */
        u->wt_morph = v;
    } else if (strcmp(key, PK_USR_LAYERVOL) == 0) {
        /* LAYER VOL: mix the wavetable body against the sample layer. */
        u->layer_vol = v;
    } else if (strcmp(key, PK_USR_PITCHENV) == 0) {
        /* PITCH ENV: extra downward sweep depth applied to the wavetable body. */
        u->pitch_env_amt = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        u->fx_type = v;
        fx_config(&u->fx, (int)(u->fx_type * 4.0f + 0.5f), u->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        u->fx_amt = v;
        fx_config(&u->fx, (int)(u->fx_type * 4.0f + 0.5f), u->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ---- Trigger (note-on) --------------------------------------------------- */
static void usr_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    usr_state *u = (usr_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&u->pitch_env_fast, 1.0f, c909);
    env_trigger(&u->pitch_env_slow, 1.0f, c808);

    float len_ms = u->length_ms > 0.0f ? u->length_ms : 400.0f;
    env_trigger(&u->amp_env, velf, env_coeff_from_ms(len_ms));

    float trs_ms = u->trs_dec_ms > 0.0f ? u->trs_dec_ms : 8.0f;
    env_trigger(&u->trs_env, 1.0f, env_coeff_from_ms(trs_ms));
    noise_seed(&u->trs_noise, 0x115E4C0DEULL ^ (uint64_t)(unsigned)velocity);

    /* Reset playback cursors + filter states for a deterministic attack. */
    u->body_phase   = 0.0f;
    u->sample_pos   = 0.0f;
    u->color_lp.s   = 0.0f;
    u->trs_tone_lp.s = 0.0f;

    u->fx.last     = 0.0f;
    u->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals here) ------------------------ */
static void usr_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    usr_state *u = (usr_state *)inst->model_state;

    /* Sample-layer source (B3): the SAMPLE SELECT bank slot if chosen, else the
     * legacy user/kick.wav one-shot (backward compat). */
    const float *src = NULL; int src_len = 0;
    if (u->sample_idx > 0 && u->sample_idx <= inst->sample_count) {
        src = inst->sample_bank[u->sample_idx - 1];
        src_len = inst->sample_len[u->sample_idx - 1];
    } else if (inst->usr_loaded && inst->usr_sample_len > 0) {
        src = inst->usr_sample;
        src_len = inst->usr_sample_len;
    }
    const bool have_sample = (src && src_len > 0);
    const bool have_usr_wt = inst->usr_wt_loaded;
    const float slen = (float)src_len;

    for (int n = 0; n < frames; n++) {
        float p_fast = env_tick(&u->pitch_env_fast);
        float p_slow = env_tick(&u->pitch_env_slow);
        float pitch  = p_slow + u->curve * (p_fast - p_slow);   /* 808<->909 lerp */
        /* PITCH ENV deepens the downward sweep on the body. */
        float fbody  = u->f0 + pitch * u->sweep_hz * (1.0f + u->pitch_env_amt);

        /* Wavetable body: read the USER table if loaded, else a built-in factory
         * wave selected by WT MORPH (fallback -> non-silent). WT MORPH also
         * blends the user table toward a folded/brighter variant. */
        float wt;
        if (have_usr_wt) {
            wt = wt_read(inst->usr_wavetable, u->body_phase);
            float folded = fabsf(wt) * 2.0f - 1.0f;
            wt = wt + u->wt_morph * (folded - wt);   /* morph timbre */
        } else {
            /* Built-in fallback: morph across sine(0)..analog(NUM_WAVES-1).
             * CROSSFADE between adjacent tables instead of rounding to the
             * nearest — rounding made WT MORPH jump discretely between waves
             * ("weird jumps", VOICE-06). Now the sweep is continuous. */
            float fpos = u->wt_morph * (float)(NUM_WAVES - 1);
            int wa = (int)fpos;
            if (wa < 0) wa = 0;
            if (wa > NUM_WAVES - 2) wa = NUM_WAVES - 2;   /* leave room for wa+1 */
            float fr = fpos - (float)wa;
            float w0 = wt_read_bl(wa,     0, u->body_phase);
            float w1 = wt_read_bl(wa + 1, 0, u->body_phase);
            wt = w0 + fr * (w1 - w0);
        }
        u->body_phase += fbody / OMEGA_SR;
        if (u->body_phase >= 1.0f) u->body_phase -= 1.0f;

        /* User one-shot sample layer (linear-interp playback at native rate). */
        float samp = 0.0f;
        if (have_sample) {
            int i = (int)u->sample_pos;
            if (i < src_len - 1) {
                float fr = u->sample_pos - (float)i;
                samp = src[i] + fr * (src[i + 1] - src[i]);
                u->sample_pos += 1.0f;
            } else if ((float)i < slen) {
                samp = src[i];
                u->sample_pos = slen;   /* hold at end (one-shot, no loop) */
            }
        }

        float amp  = env_tick(&u->amp_env) * (0.5f + 0.5f * u->sustain);

        /* SAMPLE SELECT: source timbre morph that works WITH OR WITHOUT a user
         * sample — blends the body toward an octave-up (folded) harmonic layer
         * so the control is always audible (behavior spec: SAMPLE SELECT changes
         * output). With a user sample present it also opens the sample layer. */
        float wt_harm = fabsf(wt) * 2.0f - 1.0f;                 /* octave-ish upper */
        float wt_src  = wt + u->sample_sel * (wt_harm - wt);
        float body = wt_src * amp;

        /* Sample layer (only when a user one-shot is loaded). */
        float slyr = samp * amp;

        /* LAYER VOL: when a user sample is loaded, mixes body<->sample. With no
         * sample it acts as a body-layer level so the control still moves output
         * (v=0.5 unity; a gentle +/- trim, always responsive). */
        float voice;
        if (have_sample) {
            voice = body + u->layer_vol * (slyr - body);
        } else {
            voice = body * (0.6f + 0.8f * u->layer_vol);        /* level trim */
        }

        /* Attack transient/click (independent of the body), noise-based so its
         * brightness (TRS TNE) is a real spectral lever. LP(dark)<->raw(bright)
         * blend keeps the click audible across the TNE sweep (B-04 lesson). */
        float raw  = noise_tick(&u->trs_noise) * env_tick(&u->trs_env) * u->trs_amp;
        float dark = tpt1_lp(&u->trs_tone_lp, raw, u->trs_tone_g);
        float click = dark + u->trs_tne_mix * (raw - dark);

        /* Self-limit the summed voice+click below 1.0 (clamp is a net). The click
         * LEADS the attack — prominent while the low body sine ramps from zero —
         * so TRS TNE (its brightness) governs the attack-window spectrum and the
         * transient stays separable from the sub (B-04 WTR lesson: click*1.1). */
        float s = voice * 0.5f + click * 1.1f;
        s = tpt1_lp(&u->color_lp, s, u->color_g);   /* COLOR output LP */

        int fx_mode = (int)(u->fx_type * 4.0f + 0.5f);
        s = fx_process(fx_mode, s, u->fx_amt, &u->fx);

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void usr_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    usr_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
/* SAMPLE SEL is a DYNAMIC enum (B3, SMPL-02): its options are ["None", <bank
 * names...>] built from the samples enumerated at create_instance. The other
 * three slots are fixed floats. Bounded write into buf; returns 0 on overflow. */
static int usr_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    int off = 0;
    /* SAMPLE SEL enum head + options. */
    int w = snprintf(buf + off, (size_t)(buf_len - off),
        "{\"key\":\"" PK_USR_SAMPLE "\",\"name\":\"SAMPLE SEL\",\"type\":\"enum\",\"options\":[\"None\"");
    if (w < 0 || w >= buf_len - off) return 0;
    off += w;
    int n = inst ? inst->sample_count : 0;
    for (int i = 0; i < n; i++) {
        w = snprintf(buf + off, (size_t)(buf_len - off), ",\"%s\"", inst->sample_name[i]);
        if (w < 0 || w >= buf_len - off) return 0;
        off += w;
    }
    w = snprintf(buf + off, (size_t)(buf_len - off),
        "],\"default\":0},"
        "{\"key\":\"" PK_USR_WTMORPH  "\",\"name\":\"WT MORPH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_USR_LAYERVOL "\",\"name\":\"LAYER VOL\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_USR_PITCHENV "\",\"name\":\"PITCH ENV\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}");
    if (w < 0 || w >= buf_len - off) return 0;
    off += w;
    return off;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_usr_vtable = {
    .name         = "USR",
    .trigger      = usr_trigger,
    .render       = usr_render,
    .set_param    = usr_set_param,
    .set_p2       = usr_set_p2,
    .p2_slot_desc = usr_p2_slot_desc,
};
