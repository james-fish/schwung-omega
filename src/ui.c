/* ui.c — The real levels-based ui_hierarchy (D-08/D-09).
 *
 * Owns omega_build_ui: emits the host's real ui_hierarchy schema (Context/01
 * lines 109-162) into the caller's buffer with ZERO allocation (D-09).
 *
 * Layout (pre-serialized static fragments, no JSON library):
 *   {
 *     "pad_layout":"drums",
 *     "child_index_param":"current_pad",
 *     "levels":{
 *       "root":  { name "Omega", model+master_vol knobs, kick1/kick2 sub-pages },
 *       "kick1": { name "Kick 1", the 8 Page-1 params },
 *       "kick2": { name "Kick 2", the 5 FM2 Page-2 params }
 *     }
 *   }
 *
 * The whole hierarchy is a set of fixed .rodata strings assembled in order.
 * FM2 is the only Phase A model, so its Page-2 params are inlined here directly
 * (simpler than splicing fm2.c's p2_slot_desc, and the schema shape changed).
 * fm2.c keeps its fm2_p2_slot_desc / g_fm2_vtable.p2_slot_desc field in place;
 * ui.c simply stops calling it. Do NOT touch fm2.c.
 *
 * Every write is bounded to the remaining buf_len (A-RESEARCH Pitfall 3): a
 * running offset + a bounded-append helper that never writes past buf_len and
 * always leaves room for the null terminator. Returns bytes written (excluding
 * the terminator) to match the get_param contract (A-RESEARCH 311-314); on any
 * overflow it returns what fit, still null-terminated — never overruns.
 *
 * No allocation, no file I/O, no logging here — all six entry points run on the
 * audio thread (A-RESEARCH Pitfall 1). The D-10 buf_len log lives in dsp.c.
 */
#include "omega.h"

#include <string.h>

/* Top-level wrapper: pad_layout + child_index_param + open the levels map. */
static const char UI_OPEN[] =
    "{\"pad_layout\":\"drums\",\"child_index_param\":\"current_pad\",\"levels\":{";

/* root level: Model enum + Volume float, then the two kick sub-page links. */
static const char UI_ROOT[] =
    "\"root\":{\"name\":\"Omega\",\"params\":["
      "{\"key\":\"" PK_MODEL "\",\"name\":\"Model\",\"type\":\"enum\",\"options\":"
        "[\"FM2\",\"FM4\",\"WTR\",\"PHY\",\"HRD\",\"DIG\",\"TRS\",\"ANA\",\"USR\",\"GEN\"]},"
      "{\"key\":\"" PK_MASTER_VOL "\",\"name\":\"Volume\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"level\":\"kick1\",\"label\":\"Kick 1\"},"
      "{\"level\":\"kick2\",\"label\":\"Kick 2\"}"
    "],\"knobs\":[\"" PK_MODEL "\",\"" PK_MASTER_VOL "\"]},";

/* kick1 level: the 8 Page-1 params (Bohm order), each a 0..1 float. */
static const char UI_KICK1[] =
    "\"kick1\":{\"name\":\"Kick 1\",\"params\":["
      "{\"key\":\"" PK_PITCH   "\",\"name\":\"PITCH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_LENGTH  "\",\"name\":\"LENGTH\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_SUSTAIN "\",\"name\":\"SUSTAIN\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_CURVE   "\",\"name\":\"CURVE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_ATTACK  "\",\"name\":\"ATTACK\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_TRS_DEC "\",\"name\":\"TRS DEC\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_TRS_TNE "\",\"name\":\"TRS TNE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_COLOR   "\",\"name\":\"COLOR\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}"
    "],\"knobs\":[\"" PK_PITCH "\",\"" PK_LENGTH "\",\"" PK_SUSTAIN "\",\"" PK_CURVE
      "\",\"" PK_ATTACK "\",\"" PK_TRS_DEC "\",\"" PK_TRS_TNE "\",\"" PK_COLOR "\"]},";

/* kick2 level: the 5 FM2 Page-2 params (inlined — FM2 is the only Phase A model). */
static const char UI_KICK2[] =
    "\"kick2\":{\"name\":\"Kick 2\",\"params\":["
      "{\"key\":\"" PK_FM_RATIO "\",\"name\":\"FM RATIO\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_FM_INDEX "\",\"name\":\"FM INDEX\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_OP2_WAVE "\",\"name\":\"OP2 WAVE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_FX_TYPE  "\",\"name\":\"FX TYPE\",\"type\":\"float\",\"min\":0.0,\"max\":1.0},"
      "{\"key\":\"" PK_FX_AMT   "\",\"name\":\"FX AMT\",\"type\":\"float\",\"min\":0.0,\"max\":1.0}"
    "],\"knobs\":[\"" PK_FM_RATIO "\",\"" PK_FM_INDEX "\",\"" PK_OP2_WAVE
      "\",\"" PK_FX_TYPE "\",\"" PK_FX_AMT "\"]}";

/* Close the levels map + the top-level object. */
static const char UI_CLOSE[] = "}}";

/* Bounded append: copy up to `remaining` bytes of `src` (len bytes) into
 * buf+off, never writing past buf_len-1 (reserving a byte for the terminator).
 * Advances *off by the number of bytes actually written. Safe when buf is full:
 * copies 0 and leaves *off unchanged so the final terminator still lands. */
static void ui_append(char *buf, int buf_len, int *off, const char *src, int len) {
    if (*off >= buf_len - 1) return;               /* no room (reserve terminator) */
    int room = (buf_len - 1) - *off;               /* bytes we may still write */
    int n = len < room ? len : room;               /* min(len, room) */
    memcpy(buf + *off, src, (size_t)n);
    *off += n;
}

/* Assemble the full hierarchy into `buf`, bounded to buf_len, null-terminated.
 * Returns bytes written excluding the terminator (get_param contract). */
int omega_build_ui(bohm_instance_t *inst, char *buf, int buf_len) {
    (void)inst;   /* FM2-only Phase A: the levels are static */
    if (!buf || buf_len <= 0) return 0;

    int off = 0;

    ui_append(buf, buf_len, &off, UI_OPEN,  (int)(sizeof(UI_OPEN)  - 1));
    ui_append(buf, buf_len, &off, UI_ROOT,  (int)(sizeof(UI_ROOT)  - 1));
    ui_append(buf, buf_len, &off, UI_KICK1, (int)(sizeof(UI_KICK1) - 1));
    ui_append(buf, buf_len, &off, UI_KICK2, (int)(sizeof(UI_KICK2) - 1));
    ui_append(buf, buf_len, &off, UI_CLOSE, (int)(sizeof(UI_CLOSE) - 1));

    /* Always null-terminate within bounds (Pitfall 3). off <= buf_len-1 by
     * construction, so buf[off] is a valid slot. */
    buf[off] = '\0';
    return off;
}
