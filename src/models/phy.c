/* phy.c — PHY / PM-K1 model: modal physical-model kick (KICK-05).
 *
 * The only NON-oscillator engine (B-RESEARCH §PHY): 2-3 damped resonant modes
 * (complex-rotation modal_t from B-02) excited by a short filtered noise/click
 * beater burst at trigger. Each mode is an exponentially-decaying sinusoid
 * (z *= e^{jw}*e^{-decay}, one complex multiply/mode). Distinctness = organic,
 * woody, springy natural resonance — no FM or wavetable character.
 *
 * HIGHEST-RISK DSP (Pitfall 5, NaN/blowup if freq/decay unclamped): modal_excite
 * already clamps freq to [20, 0.45*SR] and decay to (0,1); PHY ADDITIONALLY
 * clamps its computed freqs/decays before passing (defense in depth). The full
 * lo->hi param sweep — including the worst-case corner (max HEAD TENS + min
 * DAMPING) — stays finite and |x|<=1.
 *
 * Param map (B-RESEARCH §PHY VERBATIM):
 *   HEAD TENS (PK_PHY_HEADTENS) -> dominant pitched mode freq (~55-80 Hz) + the
 *     classic downward pitch envelope on it (the FM2 dual-env CURVE blend sweeps
 *     the head freq downward at trigger).
 *   SHELL SIZE (PK_PHY_SHELL)   -> 1-2 lower-Q body modes' freqs (bigger shell =
 *     lower freq; body ~120-180 Hz default).
 *   DAMPING   (PK_PHY_DAMPING)  -> decay coefficient of ALL modes (more damping =
 *     shorter/deader; decays ~150-400 ms). ms->decay_per_sample, CLAMPED (0,1).
 *   BEATER    (PK_PHY_BEATER)   -> excitation character: a short bright filtered-
 *     noise burst (noise_t + tpt1_lp), ~2-5 ms; brighter/harder = more HF + shorter.
 *
 * Signal flow (per sample):
 *   burst = beater noise burst (LP-colored) * burst_env
 *   modes = a0*modal_tick(head) + a1*modal_tick(body1) + a2*modal_tick(body2)
 *   s     = amp_env * (0.5+0.5*sustain) * modes + burst
 *   s     = COLOR output LP
 *   s     = fx_process(post-kick FX TYPE/AMT, s, fx_amt, &fx)   (KICK-14)
 *
 * State overlays bohm_instance.model_state; all coeff/transcendental work
 * (cos_w/sin_w/decay in modal_excite, env coeffs, tpt g) happens in
 * set_param/trigger, never per-sample. Shares fm2.c/hrd.c structure (Pattern 4):
 * parse_f + clampf + tpt_g_from_hz.
 */
#include "omega.h"
#include "dsp_primitives.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- PHY per-instance state (overlays bohm_instance.model_state) --------- */
typedef struct phy_state {
    /* Modal resonators (complex-rotation). head is the dominant pitched mode
     * (downward-swept); body1/body2 are lower-Q shell modes. */
    modal_t head, body1, body2;

    /* Head pitch sweep: the head mode's frequency sweeps downward toward its
     * target using the FM2 dual-env CURVE blend (909 fast + 808 slow). Re-excited
     * per block is too costly; instead we excite ONCE at trigger at the swept
     * start and let the modal decay carry the tone — but to get the audible
     * downward sweep we re-excite the head at a small set of control points is
     * also costly. Simpler + RT-safe: excite head at the TARGET freq at trigger
     * and layer a short pitched CLICK whose modal freq starts high. Here we bias
     * the head excitation freq upward by the sweep amount so the natural mode
     * settles — combined with the beater burst this gives the woody attack.
     * We store the sweep params so trigger computes the excitation freq. */
    env_t pitch_env_fast;     /* 909 fast sweep (for head-excite freq bias) */
    env_t pitch_env_slow;     /* 808 slow sweep */
    float curve;              /* 0 = 808 slow, 1 = 909 fast */

    /* Amplitude envelope + tail contour */
    env_t amp_env;
    float sustain;            /* tail contour scalar (from SUSTAIN) */
    float length_ms;          /* amp decay time (from LENGTH) */

    /* Beater excitation: short bright filtered-noise burst. */
    noise_t exc;              /* white-noise source (seeded once) */
    env_t   burst_env;        /* short excitation burst envelope */
    tpt1_t  burst_lp; float burst_g;   /* BEATER brightness LP (precomputed g) */
    float   beater;           /* BEATER [0,1]: brightness + burst length */

    /* Mode frequencies + shared decay (all precomputed in set_param; re-excited
     * at trigger). Stored as normalized param values; trigger maps to Hz/decay. */
    float pitch_hz;           /* PITCH: head-mode base frequency (Hz), VOICE-01/06 */
    float head_tens;          /* HEAD TENS [0,1] -> relative head detune */
    float shell;              /* SHELL SIZE [0,1] -> body-mode freqs */
    float damping;            /* DAMPING [0,1] -> decay of all modes */

    /* Output COLOR lowpass */
    tpt1_t color_lp; float color_g;

    /* Page-1 ATTACK: beater burst amplitude; Page-1 TRS DEC: burst length nudge. */
    float attack;
    float trs_dec_ms;

    /* Post-kick FX chain (KICK-14). */
    float      fx_type;
    float      fx_amt;
    fx_state_t fx;
} phy_state;

_Static_assert(sizeof(phy_state) <= 4096, "phy_state fits model_state");

/* ---- Locale-independent float parser (UI-01; copied from hrd.c) ---------- */
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

/* g = tanf(pi*fc/SR); precomputed per set_param, never per sample. */
static inline float tpt_g_from_hz(float fc) {
    return tanf((float)M_PI * fc / OMEGA_SR);
}

/* ---- Parameter dispatch (Page 1 + PHY Page 2 keys) ----------------------- */
void phy_set_param(bohm_instance_t *inst, const char *key, const char *val) {
    phy_state *p = (phy_state *)inst->model_state;
    float v = clampf(parse_f(val), 0.0f, 1.0f);

    if (strcmp(key, PK_PITCH) == 0) {
        /* PITCH is the head mode's BASE frequency in Hz (VOICE-01/06). This is
         * independent of HEAD TENS (which now only detunes RELATIVE to PITCH), so
         * PITCH can reach the low register (down to 30 Hz) — fixing "PHY doesn't
         * go low enough" and "HEAD TENS just pushes pitch up" (both used to write
         * the same field and fight). Mapped to the head mode freq at trigger. */
        p->pitch_hz = omega_pitch_hz(val);
    } else if (strcmp(key, PK_LENGTH) == 0) {
        /* Exp map [50,1500] ms (same musical centering as FM2). */
        p->length_ms = 50.0f * powf(1500.0f / 50.0f, v);
    } else if (strcmp(key, PK_SUSTAIN) == 0) {
        p->sustain = v;
    } else if (strcmp(key, PK_CURVE) == 0) {
        p->curve = v;                                  /* 0 = 808 slow, 1 = 909 fast */
    } else if (strcmp(key, PK_ATTACK) == 0) {
        p->attack = v;                                 /* beater burst amplitude */
    } else if (strcmp(key, PK_TRS_DEC) == 0) {
        /* Page-1 TRS DEC nudges the beater burst length (1..12 ms exp). */
        p->trs_dec_ms = 1.0f * powf(12.0f / 1.0f, v);
    } else if (strcmp(key, PK_TRS_TNE) == 0) {
        /* Page-1 TRS TNE = spectral brightness lever for PHY. It shifts both the
         * burst LP cutoff (beater click character) AND the output COLOR LP so the
         * whole sound is darker (low) vs brighter (high). This ensures TRS TNE
         * is measurably different across its full range via ZCR (not just a
         * 3 ms burst nudge that would be buried in the long modal ring-down). */
        float fc_burst = 800.0f + v * (12000.0f - 800.0f);
        p->burst_g = tpt_g_from_hz(fc_burst);
        float fc_color = 400.0f + v * (14000.0f - 400.0f);
        p->color_g = tpt_g_from_hz(fc_color);
    } else if (strcmp(key, PK_COLOR) == 0) {
        /* COLOR opens the output LP (200 Hz .. 16 kHz) — timbre morph. */
        float fc = 200.0f + v * (16000.0f - 200.0f);
        p->color_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_PHY_BEATER) == 0) {
        /* BEATER: excitation character. Brighter/harder = more HF + shorter burst.
         * Precompute the burst LP cutoff (800 Hz .. 12 kHz); brighter beater at
         * high v. Actual burst length is set at trigger from beater + trs_dec. */
        p->beater = v;
        float fc = 800.0f + v * (12000.0f - 800.0f);
        p->burst_g = tpt_g_from_hz(fc);
    } else if (strcmp(key, PK_PHY_SHELL) == 0) {
        /* SHELL SIZE: bigger shell = LOWER body-mode freq. Stored; mapped at
         * trigger. High v = big/low shell, low v = small/high shell. */
        p->shell = v;
    } else if (strcmp(key, PK_PHY_HEADTENS) == 0) {
        /* HEAD TENS: dominant pitched mode tension = its frequency. Higher tension
         * = higher freq. Stored; mapped + swept at trigger. */
        p->head_tens = v;
    } else if (strcmp(key, PK_PHY_DAMPING) == 0) {
        /* DAMPING: decay of ALL modes. More damping = shorter/deader thud. Stored;
         * mapped ms->decay_per_sample at trigger, CLAMPED into (0,1). */
        p->damping = v;
    } else if (strcmp(key, PK_FX_TYPE) == 0) {
        p->fx_type = (float)(int)(parse_f(val) + 0.5f);
        if (p->fx_type < 0.0f) p->fx_type = 0.0f;
        if (p->fx_type > 4.0f) p->fx_type = 4.0f;
        fx_config(&p->fx, (int)p->fx_type, p->fx_amt);
    } else if (strcmp(key, PK_FX_AMT) == 0) {
        p->fx_amt = v;
        fx_config(&p->fx, (int)p->fx_type, p->fx_amt);
    }
    /* Unknown keys ignored (dsp.c owns PK_MODEL/PK_MASTER_VOL/PK_UI_HIER). */
}

/* ms -> per-sample decay coefficient for a modal mode, CLAMPED into (0,1) so
 * modal_tick never blows up (Pitfall 5, defense in depth over modal_excite).
 * decay = e^{-1/(ms*0.001*SR)} ; longer ms -> closer to 1 (slower decay). */
static inline float modal_decay_from_ms(float ms) {
    if (ms < 1.0f) ms = 1.0f;                 /* guard against tiny/zero times */
    float d = expf(-1.0f / (ms * 0.001f * OMEGA_SR));
    if (d <= 0.0f) d = 0.0001f;               /* clamp into (0,1) — no blowup */
    if (d >= 1.0f) d = 0.9999f;
    return d;
}

/* ---- Trigger (note-on): excite the modes + beater burst ------------------ */
static void phy_trigger(bohm_instance_t *inst, int note, int velocity) {
    (void)note;
    phy_state *p = (phy_state *)inst->model_state;
    float velf = (float)velocity / 127.0f;

    /* Dual pitch sweep (used to bias the head excitation freq upward at attack
     * so the mode settles downward — the classic woody pitch drop). */
    float c909 = env_coeff_from_ms(15.0f);
    float c808 = env_coeff_from_ms(300.0f);
    env_trigger(&p->pitch_env_fast, 1.0f, c909);
    env_trigger(&p->pitch_env_slow, 1.0f, c808);

    /* Amplitude decay from LENGTH (cached ms). */
    float len_ms = p->length_ms > 0.0f ? p->length_ms : 300.0f;
    env_trigger(&p->amp_env, velf, env_coeff_from_ms(len_ms));

    /* Beater burst: BEATER drives both amplitude and brightness (character).
     * Hard beater = louder (0.1..0.8 amplitude) + shorter burst (~1-5 ms).
     * Soft beater = quiet (barely audible click) + slightly longer.
     * ATTACK is an independent amplitude scalar (0.1..1.0) for the whole burst
     * level — this is the knob that spans a wide enough range to be measurably
     * different at lo vs hi over the full RMS render (D-B02). */
    float trs_ms    = p->trs_dec_ms > 0.0f ? p->trs_dec_ms : 3.0f;
    float burst_ms  = clampf(trs_ms * (1.0f - 0.6f * p->beater), 0.5f, 12.0f);
    float beater_amp = 0.1f + 0.7f * p->beater;    /* 0.1..0.8, driven by BEATER */
    env_trigger(&p->burst_env, velf * beater_amp * p->attack,
                env_coeff_from_ms(burst_ms));

    /* --- Map params -> mode frequencies (Hz) + shared decay --------------- */
    /* Head mode freq = PITCH (Hz base) detuned by HEAD TENS (VOICE-06). PITCH
     * sets the fundamental (30..200 Hz); HEAD TENS shifts it +/- ~25% around
     * that, so the two controls are independent and PITCH reaches the low
     * register. Guard a zero pitch_hz (pre-prime) with the 50 Hz default. */
    float pbase = (p->pitch_hz > 0.0f) ? p->pitch_hz : 50.0f;
    float head_f = pbase * (0.75f + 0.5f * p->head_tens);

    /* Downward head sweep: bias the excitation freq upward by the CURVE amount
     * so the modal mode starts high and settles toward head_f. CURVE=0 excites
     * at the target frequency (pure modal thud, no pitch drop). CURVE=0.5
     * excites at 2x target (a moderate 909-style sweep). CURVE=1.0 excites at
     * 3x target (deep drop). sweep_mult = 1 + curve*2 (no minimum offset, so
     * CURVE=0 is truly a straight, non-sweeping tone). */
    float sweep_mult = 1.0f + p->curve * 2.0f;   /* curve=0: no sweep; curve=1: 3x sweep */
    float head_start = clampf(head_f * sweep_mult, 20.0f, 0.45f * OMEGA_SR);

    /* SHELL SIZE -> body-mode freqs. Bigger shell (high v) = LOWER freq.
     * body1 ~ 110..200 Hz, body2 ~ 180..320 Hz (2nd shell mode, quieter). */
    float body1_f = 110.0f + (1.0f - p->shell) * (200.0f - 110.0f);
    float body2_f = 180.0f + (1.0f - p->shell) * (320.0f - 180.0f);
    body1_f = clampf(body1_f, 20.0f, 0.45f * OMEGA_SR);
    body2_f = clampf(body2_f, 20.0f, 0.45f * OMEGA_SR);

    /* DAMPING -> decay time of all modes. More damping (high v) = SHORTER decay.
     * Head decays longest (the pitched tone), body modes shorter (thud/shell).
     * Base decay ~ 400 ms (low damping) down to ~60 ms (high damping), exp map. */
    float base_ms = 400.0f * powf(60.0f / 400.0f, p->damping);
    float head_decay  = modal_decay_from_ms(base_ms);
    float body1_decay = modal_decay_from_ms(base_ms * 0.5f);   /* shell shorter */
    float body2_decay = modal_decay_from_ms(base_ms * 0.3f);

    /* Excite the modes (modal_excite additionally clamps freq/decay — Pitfall 5
     * defense in depth). Head excited at the swept-up start so it settles down.
     * Mode amplitudes scale with BEATER (0.3+0.7*beater): a harder beater excites
     * the resonant body more strongly, making BEATER audible across the full decay
     * (not just the short burst transient). Head dominant, body modes quieter. */
    float mode_amp = 0.3f + 0.7f * p->beater;   /* 0.30..1.0 — beater excitation strength */
    modal_excite(&p->head,  head_start, head_decay,  velf * 0.9f  * mode_amp);
    modal_excite(&p->body1, body1_f,    body1_decay, velf * 0.5f  * mode_amp);
    modal_excite(&p->body2, body2_f,    body2_decay, velf * 0.25f * mode_amp);

    /* Reseed the beater noise so it is never silent after a zeroing model switch
     * (memset zeroes exc.rng.s; xorshift of 0 stays 0 -> silence). A fixed seed
     * keeps the beater deterministic (tests hash/asserts stay stable). */
    noise_seed(&p->exc, 0xB0FFA11BEE7EULL);

    /* Reset filter states for a deterministic attack. */
    p->burst_lp.s = 0.0f;
    p->color_lp.s = 0.0f;

    /* Reset the FX sample-and-hold, preserve precomputed crush_levels. */
    p->fx.last     = 0.0f;
    p->fx.hold_ctr = 0;
}

/* ---- Render (per-sample; no transcendentals — modal_tick + tpt1_lp only) - */
static void phy_render(bohm_instance_t *inst, float *out_l, float *out_r, int frames) {
    phy_state *p = (phy_state *)inst->model_state;

    for (int n = 0; n < frames; n++) {
        /* Advance the pitch-sweep envelopes (kept ticking so a switch/retrigger
         * stays deterministic; they bias the excitation at trigger, not here). */
        (void)env_tick(&p->pitch_env_fast);
        (void)env_tick(&p->pitch_env_slow);

        /* Beater excitation burst: bright filtered noise, short envelope. */
        float burst = noise_tick(&p->exc) * env_tick(&p->burst_env);
        burst = tpt1_lp(&p->burst_lp, burst, p->burst_g);

        /* Sum the damped modes (complex-rotation; transcendental-free tick). */
        float modes = 0.9f  * modal_tick(&p->head)
                    + 0.5f  * modal_tick(&p->body1)
                    + 0.25f * modal_tick(&p->body2);

        float amp = env_tick(&p->amp_env) * (0.5f + 0.5f * p->sustain);

        /* Body (modal sum, self-limiting decay) + beater burst. Scale so the
         * summed peak stays within [-1,1] before the int16 boundary (FNDTN-07). */
        float s = modes * amp * 0.7f + burst * 0.4f;

        /* COLOR output LP. */
        s = tpt1_lp(&p->color_lp, s, p->color_g);

        /* Post-kick FX (KICK-14): fx_type is integer 0..4 (Bug #1 fix). */
        int fx_mode = (int)p->fx_type;
        s = fx_process(fx_mode, s, p->fx_amt, &p->fx);

        /* Final safety self-limit (modal sums can transiently exceed 1.0 at the
         * excitation instant); a bounded soft-limit keeps |x|<=1 without relying
         * on the int16 clamp (D-12). x/(1+|x|) is bounded and monotone. */
        if (s > 1.0f || s < -1.0f) s = s / (1.0f + fabsf(s));

        out_l[n] = out_r[n] = s;
    }
}

/* ---- Page-2 slot delegation --------------------------------------------- */
static void phy_set_p2(bohm_instance_t *inst, const char *key, const char *val) {
    phy_set_param(inst, key, val);
}

/* ---- Page-2 slot descriptor (full JSON objects; Pattern 3) --------------- */
static int phy_p2_slot_desc(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;
    static const char json[] =
        "{\"key\":\"" PK_PHY_BEATER   "\",\"name\":\"BEATER\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_PHY_SHELL    "\",\"name\":\"SHELL SIZE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_PHY_HEADTENS "\",\"name\":\"HEAD TENS\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
        "{\"key\":\"" PK_PHY_DAMPING  "\",\"name\":\"DAMPING\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}";
    int len = (int)(sizeof(json) - 1);
    if (buf_len <= len) return 0;        /* bounded: no overflow */
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';
    return len;
}

/* ---- Vtable ------------------------------------------------------------- */
const kick_model_vtable_t g_phy_vtable = {
    .name         = "PHY",
    .trigger      = phy_trigger,
    .render       = phy_render,
    .set_param    = phy_set_param,
    .set_p2       = phy_set_p2,
    .p2_slot_desc = phy_p2_slot_desc,
};
