/* ui.c — The real minimal ui_hierarchy (D-08/D-09).
 *
 * Owns omega_build_ui: assembles the Kick Page 1 (8 encoder slots) + the
 * active model's Kick Page 2 (3 model slots + FX TYPE / FX AMT placeholders)
 * into the caller's buffer with ZERO allocation (D-09).
 *
 * Layout (pre-serialized static fragments, no JSON library):
 *   {"pages":[
 *     {"name":"Kick 1","slots":[ <UI_PAGE1: 8 {key,label}> ]},
 *     {"name":"Kick 2","slots":[ <model p2_slot_desc contents> ,
 *                                 {FX TYPE},{FX AMT} ]}
 *   ]}
 *
 * Page 1 is a single fixed .rodata string. Page 2 is dynamic: the model's
 * p2_slot_desc emits a JSON ARRAY of its model-specific slots; we splice the
 * array's INTERIOR (dropping the outer '[' ']') between our own wrapper and the
 * FX TYPE/AMT placeholder slots, so Page 2 reflects the active model (D-08,
 * A-CONTEXT integration point: ui.c reads the active model ID).
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

/* Kick Page 1: 8 encoder slots in Bohm order, each keyed with the exact PK_*
 * string so the JSON keys match set_param/get_param dispatch (D-08). Kept well
 * under 2 KB (A-RESEARCH Open Q1: conservative sizing). */
static const char UI_PAGE1[] =
    "{\"key\":\"" PK_PITCH   "\",\"label\":\"PITCH\"},"
    "{\"key\":\"" PK_LENGTH  "\",\"label\":\"LENGTH\"},"
    "{\"key\":\"" PK_SUSTAIN "\",\"label\":\"SUSTAIN\"},"
    "{\"key\":\"" PK_CURVE   "\",\"label\":\"CURVE\"},"
    "{\"key\":\"" PK_ATTACK  "\",\"label\":\"ATTACK\"},"
    "{\"key\":\"" PK_TRS_DEC "\",\"label\":\"TRS DEC\"},"
    "{\"key\":\"" PK_TRS_TNE "\",\"label\":\"TRS TNE\"},"
    "{\"key\":\"" PK_COLOR   "\",\"label\":\"COLOR\"}";

/* FX TYPE / FX AMT placeholder slots (Claude's Discretion: keys present, the 5
 * real FX modes are KICK-14 / Phase B). Emitted after the model's Page-2 slots. */
static const char UI_FX_SLOTS[] =
    ",{\"key\":\"" PK_FX_TYPE "\",\"label\":\"FX TYPE\"},"
    "{\"key\":\"" PK_FX_AMT  "\",\"label\":\"FX AMT\"}";

/* Structural wrappers. */
static const char UI_OPEN[]        = "{\"pages\":[";
static const char UI_PAGE1_OPEN[]  = "{\"name\":\"Kick 1\",\"slots\":[";
static const char UI_PAGE_MID[]    = "]},";                 /* close P1 slots+page, sep */
static const char UI_PAGE2_OPEN[]  = "{\"name\":\"Kick 2\",\"slots\":[";
static const char UI_CLOSE[]       = "]}]}";                /* close P2 slots+page+pages */

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
    if (!buf || buf_len <= 0) return 0;

    int off = 0;

    /* Opening wrapper + Page 1 (static). */
    ui_append(buf, buf_len, &off, UI_OPEN,       (int)(sizeof(UI_OPEN)       - 1));
    ui_append(buf, buf_len, &off, UI_PAGE1_OPEN, (int)(sizeof(UI_PAGE1_OPEN) - 1));
    ui_append(buf, buf_len, &off, UI_PAGE1,      (int)(sizeof(UI_PAGE1)      - 1));
    ui_append(buf, buf_len, &off, UI_PAGE_MID,   (int)(sizeof(UI_PAGE_MID)   - 1));

    /* Page 2: active model's slots, spliced from p2_slot_desc's array interior. */
    ui_append(buf, buf_len, &off, UI_PAGE2_OPEN, (int)(sizeof(UI_PAGE2_OPEN) - 1));

    char tmp[256];
    int p2 = g_models[inst->model]->p2_slot_desc(inst, tmp, (int)sizeof tmp);
    /* p2_slot_desc emits a JSON array "[ ... ]"; splice the interior (indices
     * 1..p2-2) so it drops into our own "slots":[ ... ] wrapper. Guard the
     * bounds in case a model returns an empty/short fragment. */
    if (p2 >= 2 && tmp[0] == '[' && tmp[p2 - 1] == ']') {
        ui_append(buf, buf_len, &off, tmp + 1, p2 - 2);
    }

    /* FX TYPE / FX AMT placeholders, then the closing wrapper. */
    ui_append(buf, buf_len, &off, UI_FX_SLOTS, (int)(sizeof(UI_FX_SLOTS) - 1));
    ui_append(buf, buf_len, &off, UI_CLOSE,    (int)(sizeof(UI_CLOSE)    - 1));

    /* Always null-terminate within bounds (Pitfall 3). off <= buf_len-1 by
     * construction, so buf[off] is a valid slot. */
    buf[off] = '\0';
    return off;
}
