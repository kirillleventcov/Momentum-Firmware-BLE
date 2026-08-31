#include "bl_view_list.h"
#include "../ble_locator_i.h"

#include <gui/elements.h>
#include <furi.h>
#include <string.h>
#include <stdio.h>

#define BL_ROWS_VISIBLE (3)
#define BL_ROW_HEIGHT   (17)
#define BL_HEADER_H     (12)

typedef struct {
    BlDeviceView rows[BL_SNAPSHOT_MAX];
    size_t count;
    size_t index;
    size_t top;

    BlScanMode mode;
    bool settling;
    uint8_t settle_left;
    uint8_t settle_total;
    bool scanning;
    bool logging;
    uint32_t devices;
    uint32_t matches;
    uint8_t captured;

    char group[BL_GROUP_NAME_MAX + 1];

    /* keeps the highlight glued to a device while the list re-sorts */
    uint8_t anchor_mac[6];
    bool anchor_valid;
} BlViewListModel;

struct BlViewList {
    View* view;
    BlViewListCallback callback;
    void* context;
};

/* Five bars growing upwards from `bottom`, tallest 8 px. */
static void bl_view_list_draw_signal(Canvas* canvas, int8_t rssi, uint8_t x, uint8_t bottom) {
    static const uint8_t heights[5] = {2, 3, 5, 6, 8};

    /* -100 dBm .. -40 dBm mapped to 0..5 bars */
    int level = (rssi + 100) / 12;
    if(level < 0) level = 0;
    if(level > 5) level = 5;

    for(uint8_t i = 0; i < 5; i++) {
        const uint8_t bx = x + i * 4;
        if(i < level) {
            canvas_draw_box(canvas, bx, bottom - heights[i], 3, heights[i]);
        } else {
            canvas_draw_dot(canvas, bx + 1, bottom - 1);
        }
    }
}

static void bl_view_list_draw(Canvas* canvas, void* model) {
    BlViewListModel* m = model;
    char buf[48];

    canvas_clear(canvas);

    /* --- settle screen -------------------------------------------------- */
    if(m->settling) {
        /* "Scanning candidates" in FontPrimary is wider than the screen, so the
         * countdown gets the big font and the label stays small. */
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 9, AlignCenter, AlignCenter, "Scanning candidates");

        canvas_set_font(canvas, FontPrimary);
        snprintf(buf, sizeof(buf), "%us", m->settle_left);
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, buf);

        const uint8_t bar_x = 14, bar_y = 38, bar_w = 100;
        canvas_draw_frame(canvas, bar_x, bar_y, bar_w, 9);
        if(m->settle_total) {
            const uint8_t done = (uint8_t)(((m->settle_total - m->settle_left) * (bar_w - 2)) /
                                           m->settle_total);
            if(done) canvas_draw_box(canvas, bar_x + 1, bar_y + 1, done, 7);
        }

        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "%lu found - hold still", (unsigned long)m->devices);
        canvas_draw_str_aligned(canvas, 64, 57, AlignCenter, AlignCenter, buf);
        return;
    }

    /* --- header --------------------------------------------------------- */
    canvas_draw_box(canvas, 0, 0, 128, BL_HEADER_H);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);

    if(m->mode == BlScanModeLearn) {
        snprintf(
            buf, sizeof(buf), "LEARN  captured %u/%u", m->captured, (unsigned)BL_CAPTURE_MAX);
    } else if(m->mode == BlScanModeAddMember) {
        snprintf(buf, sizeof(buf), "ADD to %s", m->group);
    } else if(m->mode == BlScanModeGroup) {
        snprintf(
            buf,
            sizeof(buf),
            "%s  %lu",
            m->group[0] ? m->group : "Any group",
            (unsigned long)m->matches);
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "ALL  %lu seen  %lu hit",
            (unsigned long)m->devices,
            (unsigned long)m->matches);
    }
    buf[21] = '\0';
    canvas_draw_str(canvas, 2, 9, buf);

    if(m->logging) canvas_draw_str(canvas, 120, 9, "L");
    if(!m->scanning) canvas_draw_str(canvas, 110, 9, "!");

    canvas_set_color(canvas, ColorBlack);

    /* --- rows ------------------------------------------------------------ */
    if(m->count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas, 64, 30, AlignCenter, AlignCenter, m->scanning ? "Scanning..." : "Not scanning");
        canvas_draw_str_aligned(
            canvas,
            64,
            42,
            AlignCenter,
            AlignCenter,
            (m->mode == BlScanModeLearn)     ? "Walk up to a device" :
            (m->mode == BlScanModeAddMember) ? "Walk up to the device" :
            (m->mode == BlScanModeGroup)     ? "No group members in range" :
                                               "Nothing on air yet");
        return;
    }

    for(size_t r = 0; r < BL_ROWS_VISIBLE; r++) {
        const size_t idx = m->top + r;
        if(idx >= m->count) break;

        const BlDeviceView* d = &m->rows[idx];
        const uint8_t y = BL_HEADER_H + r * BL_ROW_HEIGHT;
        const bool selected = (idx == m->index);

        if(selected) {
            canvas_draw_box(canvas, 0, y, 125, BL_ROW_HEIGHT - 1);
            canvas_set_color(canvas, ColorWhite);
        }

        canvas_set_font(canvas, FontSecondary);

        /* line 1: name (or MAC) on the left, signal number on the right */
        snprintf(buf, sizeof(buf), "%s%s", d->captured ? "* " : "", d->label);
        buf[18] = '\0';
        canvas_draw_str(canvas, 2, y + 7, buf);

        snprintf(buf, sizeof(buf), "%d", d->rssi);
        canvas_draw_str_aligned(canvas, 123, y + 7, AlignRight, AlignBottom, buf);

        /* line 2: why it is here and how fresh it is, then the bars */
        const uint32_t age_s = d->age_ms / 1000;
        if(d->matched) {
            snprintf(buf, sizeof(buf), "hit %d%%  %lus", d->score, (unsigned long)age_s);
        } else {
            /* last three MAC bytes are enough to tell neighbours apart */
            snprintf(
                buf,
                sizeof(buf),
                "%02X:%02X:%02X  %lus",
                d->mac[3],
                d->mac[4],
                d->mac[5],
                (unsigned long)age_s);
        }
        buf[16] = '\0';
        canvas_draw_str(canvas, 2, y + 15, buf);

        bl_view_list_draw_signal(canvas, d->rssi, 100, y + 15);

        if(selected) canvas_set_color(canvas, ColorBlack);
    }

    elements_scrollbar(canvas, m->index, m->count);
}

static void bl_view_list_move(BlViewListModel* m, int delta) {
    if(m->count == 0) return;

    if(delta < 0 && m->index == 0) {
        m->index = m->count - 1;
    } else if(delta > 0 && m->index + 1 >= m->count) {
        m->index = 0;
    } else {
        m->index = (size_t)((int)m->index + delta);
    }

    if(m->index < m->top) {
        m->top = m->index;
    } else if(m->index >= m->top + BL_ROWS_VISIBLE) {
        m->top = m->index - BL_ROWS_VISIBLE + 1;
    }

    memcpy(m->anchor_mac, m->rows[m->index].mac, 6);
    m->anchor_valid = true;
}

static bool bl_view_list_input(InputEvent* event, void* context) {
    BlViewList* view_list = context;
    bool consumed = false;
    uint32_t emit = 0;

    /* The settle window exists so the order stops moving under your thumb;
     * acting on a half-built list would defeat it. Back still gets out. */
    bool settling = false;
    with_view_model(view_list->view, BlViewListModel * m, { settling = m->settling; }, false);
    if(settling) return (event->key != InputKeyBack);

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                view_list->view, BlViewListModel * m, { bl_view_list_move(m, -1); }, true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                view_list->view, BlViewListModel * m, { bl_view_list_move(m, 1); }, true);
            consumed = true;
        }
    }

    if(event->type == InputTypeShort) {
        bool empty = true;
        bool learn = false;
        bool picking = false;
        with_view_model(
            view_list->view,
            BlViewListModel * m,
            {
                empty = (m->count == 0);
                learn = (m->mode == BlScanModeLearn);
                picking = (m->mode == BlScanModeAddMember);
            },
            false);

        if(event->key == InputKeyOk && !empty) {
            emit = (learn || picking) ? BlCustomEventCapture : BlCustomEventSelect;
            consumed = true;
        } else if(event->key == InputKeyRight && !empty) {
            emit = learn ? BlCustomEventBuild : BlCustomEventInfo;
            consumed = true;
        } else if(event->key == InputKeyLeft && learn) {
            emit = BlCustomEventUndo;
            consumed = true;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyLeft) {
        /* Walked somewhere else? Re-run the settle and re-sort from here. */
        emit = BlCustomEventResettle;
        consumed = true;
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        bool empty = true;
        with_view_model(
            view_list->view, BlViewListModel * m, { empty = (m->count == 0); }, false);
        if(!empty) {
            emit = BlCustomEventInfo;
            consumed = true;
        }
    }

    if(emit && view_list->callback) {
        view_list->callback(emit, view_list->context);
    }

    return consumed;
}

BlViewList* bl_view_list_alloc(void) {
    BlViewList* view_list = malloc(sizeof(BlViewList));
    memset(view_list, 0, sizeof(BlViewList));

    view_list->view = view_alloc();
    view_allocate_model(view_list->view, ViewModelTypeLocking, sizeof(BlViewListModel));
    view_set_context(view_list->view, view_list);
    view_set_draw_callback(view_list->view, bl_view_list_draw);
    view_set_input_callback(view_list->view, bl_view_list_input);

    return view_list;
}

void bl_view_list_free(BlViewList* view_list) {
    furi_check(view_list);
    view_free(view_list->view);
    free(view_list);
}

View* bl_view_list_get_view(BlViewList* view_list) {
    return view_list->view;
}

void bl_view_list_set_callback(BlViewList* view_list, BlViewListCallback callback, void* context) {
    view_list->callback = callback;
    view_list->context = context;
}

void bl_view_list_set_mode(BlViewList* view_list, BlScanMode mode) {
    with_view_model(view_list->view, BlViewListModel * m, { m->mode = mode; }, true);
}

void bl_view_list_set_status(
    BlViewList* view_list,
    uint32_t devices,
    uint32_t matches,
    uint8_t captured,
    bool scanning,
    bool logging) {
    with_view_model(
        view_list->view,
        BlViewListModel * m,
        {
            m->devices = devices;
            m->matches = matches;
            m->captured = captured;
            m->scanning = scanning;
            m->logging = logging;
        },
        true);
}

void bl_view_list_set_settling(
    BlViewList* view_list,
    bool settling,
    uint8_t remaining_s,
    uint8_t total_s) {
    with_view_model(
        view_list->view,
        BlViewListModel * m,
        {
            m->settling = settling;
            m->settle_left = remaining_s;
            m->settle_total = total_s;
        },
        true);
}

void bl_view_list_set_group(BlViewList* view_list, const char* group) {
    with_view_model(
        view_list->view,
        BlViewListModel * m,
        { snprintf(m->group, sizeof(m->group), "%s", group ? group : ""); },
        false);
}

void bl_view_list_set_rows(BlViewList* view_list, const BlDeviceView* rows, size_t count) {
    if(count > BL_SNAPSHOT_MAX) count = BL_SNAPSHOT_MAX;

    with_view_model(
        view_list->view,
        BlViewListModel * m,
        {
            if(count) memcpy(m->rows, rows, count * sizeof(BlDeviceView));
            m->count = count;

            /* Follow the previously selected device across re-sorts so the
             * highlight does not jump around while the list reshuffles. */
            if(m->anchor_valid) {
                bool found = false;
                for(size_t i = 0; i < count; i++) {
                    if(memcmp(m->rows[i].mac, m->anchor_mac, 6) == 0) {
                        m->index = i;
                        found = true;
                        break;
                    }
                }
                if(!found) m->anchor_valid = false;
            }

            if(!m->anchor_valid) {
                if(m->index >= count) m->index = count ? count - 1 : 0;
                if(count) {
                    memcpy(m->anchor_mac, m->rows[m->index].mac, 6);
                    m->anchor_valid = true;
                }
            }

            if(m->index < m->top) {
                m->top = m->index;
            } else if(m->index >= m->top + BL_ROWS_VISIBLE) {
                m->top = m->index - BL_ROWS_VISIBLE + 1;
            }
            if(m->top + BL_ROWS_VISIBLE > count) {
                m->top = (count > BL_ROWS_VISIBLE) ? (count - BL_ROWS_VISIBLE) : 0;
            }
        },
        true);
}

bool bl_view_list_get_selected(BlViewList* view_list, uint8_t mac[6]) {
    bool ok = false;

    with_view_model(
        view_list->view,
        BlViewListModel * m,
        {
            if(m->count > 0 && m->index < m->count) {
                memcpy(mac, m->rows[m->index].mac, 6);
                ok = true;
            }
        },
        false);

    return ok;
}

void bl_view_list_reset(BlViewList* view_list) {
    with_view_model(
        view_list->view,
        BlViewListModel * m,
        {
            m->count = 0;
            m->index = 0;
            m->top = 0;
            m->anchor_valid = false;
            m->captured = 0;
        },
        true);
}
