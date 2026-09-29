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
typedef enum { UP_FLOAT, UP_ENUM } up_type_t;
typedef struct {
    const char *key;
    const char *name;
    const char *shortn;      /* <=6 chars for the OLED */
    up_type_t   type;
    const char *unit;        /* "", "%", "Hz", "ms" ... */
    const char *step;        /* JSON number literal, e.g. "0.01" */
    const char *options;     /* enum: pre-serialized JSON array; else NULL */
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
    char obj[320];
    if (p->type == UP_ENUM) {
        pk_format_value(ui_default_for(p->key), 0, defbuf, (int)sizeof defbuf);
        snprintf(obj, sizeof obj,
            "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
            "\"type\":\"enum\",\"options\":%s,\"default\":%s}",
            p->key, p->name, p->shortn, p->options, defbuf);
    } else {
        pk_format_value(ui_default_for(p->key), 4, defbuf, (int)sizeof defbuf);
        snprintf(obj, sizeof obj,
            "{\"key\":\"%s\",\"name\":\"%s\",\"short_name\":\"%s\","
            "\"type\":\"float\",\"min\":0.0,\"max\":1.0,\"default\":%s,"
            "\"step\":%s,\"unit\":\"%s\"}",
            p->key, p->name, p->shortn, defbuf, p->step, p->unit);
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
static const char OPT_MODEL[] =
    "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\",\"GEN\"]";
static const char OPT_FX[]    = "[\"Diode\",\"Clip\",\"SAT\",\"Fold\",\"Crush\"]";
static const char OPT_MONO[]  = "[\"Stereo\",\"Mono\"]";
static const char OPT_POLE[]  = "[\"2-pole\",\"4-pole\"]";

/* ---- level tables -------------------------------------------------------- */
static const uiparam_t P_ROOT[] = {
    { PK_MODEL,      "Model",  "MODEL", UP_ENUM,  "",  "0", OPT_MODEL },
    { PK_MASTER_VOL, "Volume", "VOL",   UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_ROOT[] = "[\"" PK_MODEL "\",\"" PK_MASTER_VOL "\"]";

static const uiparam_t P_KICK1[] = {
    { PK_PITCH,   "PITCH",   "PITCH", UP_FLOAT, "%", "0.01", NULL },
    { PK_LENGTH,  "LENGTH",  "LEN",   UP_FLOAT, "%", "0.01", NULL },
    { PK_SUSTAIN, "SUSTAIN", "SUS",   UP_FLOAT, "%", "0.01", NULL },
    { PK_CURVE,   "CURVE",   "CURVE", UP_FLOAT, "%", "0.01", NULL },
    { PK_ATTACK,  "ATTACK",  "ATK",   UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_DEC, "TRS DEC", "TRSDEC",UP_FLOAT, "%", "0.01", NULL },
    { PK_TRS_TNE, "TRS TNE", "TRSTNE",UP_FLOAT, "%", "0.01", NULL },
    { PK_COLOR,   "COLOR",   "COLOR", UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_KICK1[] =
    "[\"" PK_PITCH "\",\"" PK_LENGTH "\",\"" PK_SUSTAIN "\",\"" PK_CURVE
    "\",\"" PK_ATTACK "\",\"" PK_TRS_DEC "\",\"" PK_TRS_TNE "\",\"" PK_COLOR "\"]";

/* Kick Page 2 FX suffix params (appended after the model splice). */
static const uiparam_t P_FX[] = {
    { PK_FX_TYPE, "FX TYPE", "FXTYPE", UP_ENUM,  "",  "0",    OPT_FX },
    { PK_FX_AMT,  "FX AMT",  "FXAMT",  UP_FLOAT, "%", "0.01", NULL },
};
static const char KN_FX[] = "[\"" PK_FX_TYPE "\",\"" PK_FX_AMT "\"]";

static const uiparam_t P_GROOVE1[] = {
    { PK_GRV_VOL,    "VOL",    "VOL",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_LENGTH, "LENGTH", "LEN",  UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_COLOR,  "COLOR",  "COLOR",UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP1,   "TAP1",   "TAP1", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP2,   "TAP2",   "TAP2", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP3,   "TAP3",   "TAP3", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_TAP4,   "TAP4",   "TAP4", UP_FLOAT, "%", "0.01", NULL },
    { PK_GRV_MONO,   "MONO",   "MONO", UP_ENUM,  "",  "0",    OPT_MONO },
};
static const char KN_GROOVE1[] =
    "[\"" PK_GRV_VOL "\",\"" PK_GRV_LENGTH "\",\"" PK_GRV_COLOR "\",\"" PK_GRV_TAP1
    "\",\"" PK_GRV_TAP2 "\",\"" PK_GRV_TAP3 "\",\"" PK_GRV_TAP4 "\",\"" PK_GRV_MONO "\"]";

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
    "{\"pad_layout\":\"drums\",\"child_index_param\":\"current_pad\",\"levels\":{";
/* root has two nav links to the kick sub-pages appended before its knobs; keep
 * them as fixed fragments spliced into the root level. */
static const char UI_ROOT_LINKS[] =
    ",{\"level\":\"kick1\",\"label\":\"Kick 1\"},"
    "{\"level\":\"kick2\",\"label\":\"Kick 2\"}";
static const char UI_KICK2_PREFIX[] = "\"kick2\":{\"name\":\"Kick 2\",\"params\":[";
static const char UI_CLOSE[] = "}}";

#define NELEM(a) ((int)(sizeof(a)/sizeof((a)[0])))

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

    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "kick1", "Kick 1", P_KICK1, NELEM(P_KICK1), KN_KICK1);

    /* kick2 = prefix + model splice + FX suffix (with leading comma iff the
     * model emitted an interior). */
    ui_puts(buf, buf_len, &off, ",");
    ui_puts(buf, buf_len, &off, UI_KICK2_PREFIX);
    if (slot_len > 0) {
        ui_append(buf, buf_len, &off, scratch, slot_len);
        ui_puts(buf, buf_len, &off, ",");
    }
    ui_emit_param(buf, buf_len, &off, &P_FX[0]);   /* FX TYPE */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_param(buf, buf_len, &off, &P_FX[1]);   /* FX AMT */
    ui_puts(buf, buf_len, &off, "],\"knobs\":");
    ui_puts(buf, buf_len, &off, KN_FX);
    ui_puts(buf, buf_len, &off, "}");

    /* Groove Page 1 always; Groove Page 2 only for GEN (GRV-04). */
    ui_puts(buf, buf_len, &off, ",");
    ui_emit_level(buf, buf_len, &off, "groove1", "Groove 1", P_GROOVE1, NELEM(P_GROOVE1), KN_GROOVE1);
    if (inst && inst->model == MODEL_GEN) {
        ui_puts(buf, buf_len, &off, ",");
        ui_emit_level(buf, buf_len, &off, "groove2", "Groove 2", P_GROOVE2, NELEM(P_GROOVE2), KN_GROOVE2);
    }

    ui_puts(buf, buf_len, &off, UI_CLOSE);
    buf[off] = '\0';
    return off;
}
