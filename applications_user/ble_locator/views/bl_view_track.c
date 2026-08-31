#include "bl_view_track.h"
#include "../helpers/bl_adv.h"

#include <gui/elements.h>
#include <furi.h>
#include <string.h>
#include <stdio.h>

#define BL_TREND_HISTORY (8)

typedef struct {
    char label[BL_NAME_MAX + 1];
    char mac[20];
    char group[BL_GROUP_NAME_MAX + 1];
    char distance[12];

    int8_t rssi;
    int8_t rssi_peak;
    int8_t history[BL_TREND_HISTORY];
    uint8_t history_len;
    int8_t trend;

    uint32_t age_ms;
    uint16_t pps_x10;
    int8_t score;
    bool present;
} BlViewTrackModel;

struct BlViewTrack {
    View* view;
};

static void bl_view_track_draw(Canvas* canvas, void* model) {
    BlViewTrackModel* m = model;
    char buf[40];

    canvas_clear(canvas);

    /* --- title ---------------------------------------------------------- */
    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "%s", m->label);
    buf[21] = '\0';
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, buf);

    /* --- big signal reading --------------------------------------------- */
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%d", m->rssi);
    canvas_draw_str_aligned(canvas, 4, 32, AlignLeft, AlignBottom, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 4, 40, "dBm");

    /* --- distance + trend ------------------------------------------------ */
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 124, 22, AlignRight, AlignBottom, m->distance);

    canvas_set_font(canvas, FontSecondary);
    const char* trend_text = (m->trend > 0) ? "CLOSER" : (m->trend < 0) ? "FURTHER" : "steady";
    canvas_draw_str_aligned(canvas, 124, 32, AlignRight, AlignBottom, trend_text);

    snprintf(buf, sizeof(buf), "peak %d", m->rssi_peak);
    canvas_draw_str_aligned(canvas, 124, 41, AlignRight, AlignBottom, buf);

    /* --- proximity bar --------------------------------------------------- */
    const uint8_t bar_x = 2;
    const uint8_t bar_y = 44;
    const uint8_t bar_w = 124;
    const uint8_t bar_h = 9;

    canvas_draw_frame(canvas, bar_x, bar_y, bar_w, bar_h);

    int fill = ((int)m->rssi + 100) * (bar_w - 2) / 60;
    if(fill < 0) fill = 0;
    if(fill > bar_w - 2) fill = bar_w - 2;
    if(fill > 0) canvas_draw_box(canvas, bar_x + 1, bar_y + 1, (uint8_t)fill, bar_h - 2);

    int peak = ((int)m->rssi_peak + 100) * (bar_w - 2) / 60;
    if(peak < 0) peak = 0;
    if(peak > bar_w - 2) peak = bar_w - 2;
    canvas_draw_line(
        canvas, bar_x + 1 + peak, bar_y - 2, bar_x + 1 + peak, bar_y + bar_h + 1);

    /* --- footer ---------------------------------------------------------- */
    canvas_set_font(canvas, FontSecondary);
    if(!m->present) {
        canvas_draw_str(canvas, 2, 63, "LOST - keep walking");
    } else {
        snprintf(
            buf,
            sizeof(buf),
            "%s  %u.%u/s",
            (m->score >= 0 && m->group[0]) ? m->group : m->mac,
            (unsigned)(m->pps_x10 / 10),
            (unsigned)(m->pps_x10 % 10));
        buf[27] = '\0';
        canvas_draw_str(canvas, 2, 63, buf);
    }
}

BlViewTrack* bl_view_track_alloc(void) {
    BlViewTrack* view_track = malloc(sizeof(BlViewTrack));
    memset(view_track, 0, sizeof(BlViewTrack));

    view_track->view = view_alloc();
    view_allocate_model(view_track->view, ViewModelTypeLocking, sizeof(BlViewTrackModel));
    view_set_context(view_track->view, view_track);
    view_set_draw_callback(view_track->view, bl_view_track_draw);

    return view_track;
}

void bl_view_track_free(BlViewTrack* view_track) {
    furi_check(view_track);
    view_free(view_track->view);
    free(view_track);
}

View* bl_view_track_get_view(BlViewTrack* view_track) {
    return view_track->view;
}

void bl_view_track_update(
    BlViewTrack* view_track,
    const BlDeviceView* device,
    const char* group_name,
    const char* distance_hint,
    bool present) {
    with_view_model(
        view_track->view,
        BlViewTrackModel * m,
        {
            snprintf(m->label, sizeof(m->label), "%s", device->label);
            snprintf(m->group, sizeof(m->group), "%s", group_name ? group_name : "");
            snprintf(m->distance, sizeof(m->distance), "%s", distance_hint ? distance_hint : "");
            bl_mac_to_string(device->mac, m->mac, sizeof(m->mac));

            m->rssi = device->rssi;
            m->rssi_peak = device->rssi_peak;
            m->age_ms = device->age_ms;
            m->pps_x10 = device->pps_x10;
            m->score = device->score;
            m->present = present;

            /* Trend from the mean of the two halves of a short history - a
             * single sample is far too noisy to steer someone by. */
            if(m->history_len < BL_TREND_HISTORY) {
                m->history[m->history_len++] = device->rssi;
            } else {
                memmove(m->history, m->history + 1, BL_TREND_HISTORY - 1);
                m->history[BL_TREND_HISTORY - 1] = device->rssi;
            }

            if(m->history_len >= 4) {
                const uint8_t half = m->history_len / 2;
                int old_sum = 0;
                int new_sum = 0;
                for(uint8_t i = 0; i < half; i++) old_sum += m->history[i];
                for(uint8_t i = m->history_len - half; i < m->history_len; i++)
                    new_sum += m->history[i];
                const int delta = (new_sum - old_sum) / (int)half;
                m->trend = (delta >= 3) ? 1 : (delta <= -3) ? -1 : 0;
            } else {
                m->trend = 0;
            }
        },
        true);
}

void bl_view_track_reset(BlViewTrack* view_track) {
    with_view_model(
        view_track->view,
        BlViewTrackModel * m,
        {
            memset(m, 0, sizeof(BlViewTrackModel));
            m->rssi = -100;
            m->rssi_peak = -100;
        },
        true);
}
