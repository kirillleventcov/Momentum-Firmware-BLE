#include "bl_scanner.h"

#include <furi_hal_bt.h>
#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>
#include <toolbox/stream/buffered_file_stream.h>

#include <string.h>
#include <stdio.h>

#define TAG "BlScanner"

#define BL_QUEUE_DEPTH  (48)
#define BL_RSSI_ALPHA   (0.35f)
#define BL_RATE_WINDOW  (2000) /* ms window used for the packets/s readout */
#define BL_WORKER_STACK (3072)
#define BL_STALE_MS     (30000)

/* Scan parameters, in units of 0.625 ms */
#define BL_INTERVAL_FAST (0x00A0) /* 100 ms */
#define BL_WINDOW_FAST   (0x0090) /*  90 ms -> 90% duty cycle */
#define BL_INTERVAL_LOW  (0x0200) /* 320 ms */
#define BL_WINDOW_LOW    (0x0060) /*  60 ms */

typedef struct {
    uint8_t event_type;
    uint8_t address_type;
    uint8_t address[6];
    int8_t rssi;
    uint8_t data_len;
    uint8_t data[BL_RAW_MAX];
} BlRawReport;

typedef struct {
    BlFeatures f;
    int8_t rssi_last;
    int8_t rssi_peak;
    float rssi_avg;
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t packets;
    uint32_t rate_mark;
    uint16_t rate_count;
    uint16_t pps_x10;
    int8_t score;
    int8_t group_idx;
    bool in_use;
    bool captured;
} BlDeviceSlot;

struct BlScanner {
    BlDeviceSlot devices[BL_DEV_MAX];
    BlGroupList groups;

    FuriMutex* mutex;
    FuriMessageQueue* queue;
    FuriThread* worker;
    volatile bool running;
    volatile bool worker_run;

    BlScanStats stats;

    Stream* log_stream;
    bool log_active;
};

/* --- helpers ------------------------------------------------------------- */

static uint32_t bl_now(void) {
    return furi_get_tick();
}

static uint32_t bl_elapsed(uint32_t since) {
    const uint32_t now = bl_now();
    return (now >= since) ? (now - since) : 0;
}

static BlDeviceSlot* bl_scanner_find(BlScanner* s, const uint8_t mac[6], uint8_t addr_type) {
    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        BlDeviceSlot* d = &s->devices[i];
        if(d->in_use && d->f.addr_type == addr_type && memcmp(d->f.mac, mac, 6) == 0) {
            return d;
        }
    }
    return NULL;
}

/* Free slot, or the least interesting one: unmatched and uncaptured devices go
 * first, oldest within each class. A matched device is never evicted for a
 * random passing phone. */
static BlDeviceSlot* bl_scanner_claim(BlScanner* s) {
    BlDeviceSlot* best = NULL;
    int best_rank = 0;
    uint32_t best_seen = 0;

    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        BlDeviceSlot* d = &s->devices[i];
        if(!d->in_use) return d;

        /* rank 0: plain unknown, 1: matched, 2: captured */
        const int rank = d->captured ? 2 : (d->group_idx >= 0 ? 1 : 0);
        if(!best || rank < best_rank || (rank == best_rank && d->last_seen < best_seen)) {
            best = d;
            best_rank = rank;
            best_seen = d->last_seen;
        }
    }

    return best;
}

static void bl_scanner_rescore(BlScanner* s, BlDeviceSlot* d) {
    int best_score = -1;
    int best_idx = -1;

    for(uint8_t i = 0; i < s->groups.count; i++) {
        const BlGroup* p = &s->groups.items[i];
        if(!p->enabled) continue;
        const int score = bl_group_score(p, &d->f);
        if(score < 0 || score < (int)p->threshold) continue;
        if(score > best_score) {
            best_score = score;
            best_idx = i;
        }
    }

    d->score = (int8_t)best_score;
    d->group_idx = (int8_t)best_idx;
}

static void bl_scanner_log_report(BlScanner* s, const BlRawReport* r, const BlDeviceSlot* d) {
    if(!s->log_active || !s->log_stream) return;

    char mac_str[20];
    char hex[BL_RAW_MAX * 2 + 1];
    bl_mac_to_string(d->f.mac, mac_str, sizeof(mac_str));
    bl_hex_to_string(r->data, r->data_len, hex, sizeof(hex), false);

    FuriString* line = furi_string_alloc();
    furi_string_printf(
        line,
        "%lu,%s,%u,%u,%d,%s,%s,%d\n",
        (unsigned long)bl_now(),
        mac_str,
        r->address_type,
        r->event_type,
        r->rssi,
        hex,
        d->f.name,
        d->score);
    stream_write_string(s->log_stream, line);
    furi_string_free(line);
}

static void bl_scanner_ingest(BlScanner* s, const BlRawReport* r) {
    uint8_t mac[6];
    for(uint8_t i = 0; i < 6; i++) {
        mac[i] = r->address[5 - i];
    }

    furi_mutex_acquire(s->mutex, FuriWaitForever);

    BlDeviceSlot* d = bl_scanner_find(s, mac, r->address_type);
    if(!d) {
        d = bl_scanner_claim(s);
        if(!d) {
            furi_mutex_release(s->mutex);
            return;
        }
        memset(d, 0, sizeof(BlDeviceSlot));
        d->in_use = true;
        d->first_seen = bl_now();
        d->rate_mark = d->first_seen;
        d->rssi_avg = (float)r->rssi;
        d->rssi_peak = -128;
        d->score = -1;
        d->group_idx = -1;
    }

    FuriHalBtAdvReport report;
    report.event_type = r->event_type;
    report.address_type = r->address_type;
    report.rssi = r->rssi;
    report.data_len = r->data_len;
    report.data = r->data;
    memcpy(report.address, r->address, 6);

    const bool changed = bl_features_apply(&d->f, &report);

    d->rssi_last = r->rssi;
    if(r->rssi != 127) {
        d->rssi_avg = d->rssi_avg + BL_RSSI_ALPHA * ((float)r->rssi - d->rssi_avg);
        if(r->rssi > d->rssi_peak) d->rssi_peak = r->rssi;
    }
    d->last_seen = bl_now();
    d->packets++;
    d->rate_count++;

    const uint32_t window = bl_elapsed(d->rate_mark);
    if(window >= BL_RATE_WINDOW) {
        d->pps_x10 = (uint16_t)(((uint32_t)d->rate_count * 10000UL) / window);
        d->rate_count = 0;
        d->rate_mark = bl_now();
    }

    if(changed || d->score < 0) {
        bl_scanner_rescore(s, d);
    }

    bl_scanner_log_report(s, r, d);

    furi_mutex_release(s->mutex);
}

/* --- BLE callback (runs on the BLE event thread - keep it trivial) -------- */

static void bl_scanner_adv_callback(const FuriHalBtAdvReport* report, void* context) {
    BlScanner* s = context;

    BlRawReport raw;
    raw.event_type = report->event_type;
    raw.address_type = report->address_type;
    raw.rssi = report->rssi;
    memcpy(raw.address, report->address, 6);
    raw.data_len = (report->data_len > BL_RAW_MAX) ? BL_RAW_MAX : report->data_len;
    if(raw.data_len) memcpy(raw.data, report->data, raw.data_len);

    s->stats.reports++;
    if(furi_message_queue_put(s->queue, &raw, 0) != FuriStatusOk) {
        s->stats.dropped++;
    }
}

/* --- worker -------------------------------------------------------------- */

static int32_t bl_scanner_worker(void* context) {
    BlScanner* s = context;
    BlRawReport raw;

    while(s->worker_run) {
        if(furi_message_queue_get(s->queue, &raw, 100) == FuriStatusOk) {
            bl_scanner_ingest(s, &raw);
        }
    }

    return 0;
}

/* --- public API ---------------------------------------------------------- */

BlScanner* bl_scanner_alloc(void) {
    BlScanner* s = malloc(sizeof(BlScanner));
    memset(s, 0, sizeof(BlScanner));

    s->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    s->queue = furi_message_queue_alloc(BL_QUEUE_DEPTH, sizeof(BlRawReport));

    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        s->devices[i].score = -1;
        s->devices[i].group_idx = -1;
    }

    return s;
}

void bl_scanner_free(BlScanner* s) {
    furi_check(s);
    bl_scanner_stop(s);
    bl_scanner_log_stop(s);
    furi_message_queue_free(s->queue);
    furi_mutex_free(s->mutex);
    free(s);
}

void bl_scanner_set_groups(BlScanner* s, const BlGroupList* list) {
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->groups = *list;
    /* Anything already in the table has to be judged again */
    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        if(s->devices[i].in_use) bl_scanner_rescore(s, &s->devices[i]);
    }
    furi_mutex_release(s->mutex);
}

bool bl_scanner_start(BlScanner* s, bool active_scan, bool low_power) {
    if(s->running) return true;

    furi_message_queue_reset(s->queue);
    s->stats.reports = 0;
    s->stats.dropped = 0;
    s->stats.started_at = bl_now();

    s->worker_run = true;
    s->worker = furi_thread_alloc_ex("BlScanWorker", BL_WORKER_STACK, bl_scanner_worker, s);
    furi_thread_start(s->worker);

    const FuriHalBtScanConfig config = {
        .scan_interval = low_power ? BL_INTERVAL_LOW : BL_INTERVAL_FAST,
        .scan_window = low_power ? BL_WINDOW_LOW : BL_WINDOW_FAST,
        .active_scan = active_scan,
        /* Never let the controller de-duplicate: RSSI updates are the whole
         * point and a filtered device would go silent after its first packet. */
        .filter_duplicates = false,
    };

    if(!furi_hal_bt_start_scan(&config, bl_scanner_adv_callback, s)) {
        FURI_LOG_E(TAG, "furi_hal_bt_start_scan failed");
        s->worker_run = false;
        furi_thread_join(s->worker);
        furi_thread_free(s->worker);
        s->worker = NULL;
        return false;
    }

    s->running = true;
    return true;
}

void bl_scanner_stop(BlScanner* s) {
    if(!s->running) return;

    furi_hal_bt_stop_scan();
    s->running = false;

    s->worker_run = false;
    if(s->worker) {
        furi_thread_join(s->worker);
        furi_thread_free(s->worker);
        s->worker = NULL;
    }
}

bool bl_scanner_is_running(const BlScanner* s) {
    return s->running;
}

void bl_scanner_clear(BlScanner* s) {
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    memset(s->devices, 0, sizeof(s->devices));
    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        s->devices[i].score = -1;
        s->devices[i].group_idx = -1;
    }
    furi_mutex_release(s->mutex);
}

static void bl_scanner_fill_view(const BlScanner* s, const BlDeviceSlot* d, BlDeviceView* v) {
    memcpy(v->mac, d->f.mac, 6);
    bl_features_label(&d->f, v->label, sizeof(v->label));
    v->rssi = (int8_t)d->rssi_avg;
    v->rssi_peak = d->rssi_peak;
    v->score = d->score;
    v->group_idx = d->group_idx;
    v->age_ms = bl_elapsed(d->last_seen);
    v->packets = d->packets;
    v->pps_x10 = d->pps_x10;
    v->captured = d->captured;
    v->matched = (d->group_idx >= 0);
    UNUSED(s);
}

size_t bl_scanner_snapshot(
    BlScanner* s,
    BlDeviceView* out,
    size_t max,
    bool only_matched,
    bool matched_first,
    int8_t rssi_min,
    uint32_t max_age_ms) {
    size_t count = 0;
    uint32_t devices = 0;
    uint32_t matches = 0;

    furi_mutex_acquire(s->mutex, FuriWaitForever);

    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        const BlDeviceSlot* d = &s->devices[i];
        if(!d->in_use) continue;

        /* Counters describe what is on air right now, not the whole history */
        if(bl_elapsed(d->last_seen) <= BL_STALE_MS) {
            devices++;
            if(d->group_idx >= 0) matches++;
        }

        if(only_matched && d->group_idx < 0) continue;
        if((int8_t)d->rssi_avg < rssi_min) continue;
        if(max_age_ms && bl_elapsed(d->last_seen) > max_age_ms) continue;

        BlDeviceView view;
        bl_scanner_fill_view(s, d, &view);

        /* insertion sort: optionally hits first, then by smoothed RSSI */
        size_t pos = 0;
        while(pos < count) {
            bool better;
            if(matched_first && (view.matched != out[pos].matched)) {
                better = view.matched;
            } else {
                better = (view.rssi > out[pos].rssi);
            }
            if(better) break;
            pos++;
        }

        if(pos >= max) continue;

        const size_t last = (count < max) ? count : (max - 1);
        for(size_t k = last; k > pos; k--) {
            out[k] = out[k - 1];
        }
        out[pos] = view;
        if(count < max) count++;
    }

    s->stats.devices = devices;
    s->stats.matches = matches;

    furi_mutex_release(s->mutex);
    return count;
}

static bool bl_capture_set_contains(const BlCaptureSet* set, const uint8_t mac[6]) {
    if(!set) return false;
    for(uint8_t i = 0; i < set->count; i++) {
        if(memcmp(set->items[i].mac, mac, 6) == 0) return true;
    }
    return false;
}

size_t bl_scanner_score_others(
    BlScanner* s,
    const BlGroup* group,
    const BlCaptureSet* exclude,
    int8_t* scores_out,
    size_t max_scores) {
    size_t count = 0;

    furi_mutex_acquire(s->mutex, FuriWaitForever);

    for(size_t i = 0; i < BL_DEV_MAX && count < max_scores; i++) {
        const BlDeviceSlot* d = &s->devices[i];
        if(!d->in_use) continue;
        /* Only judge against what is actually on air now */
        if(bl_elapsed(d->last_seen) > BL_STALE_MS) continue;
        if(bl_capture_set_contains(exclude, d->f.mac)) continue;

        int score = bl_group_score(group, &d->f);
        if(score > 127) score = 127;
        scores_out[count++] = (int8_t)score;
    }

    furi_mutex_release(s->mutex);
    return count;
}

bool bl_scanner_get_view(BlScanner* s, const uint8_t mac[6], BlDeviceView* out) {
    bool found = false;
    furi_mutex_acquire(s->mutex, FuriWaitForever);

    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        const BlDeviceSlot* d = &s->devices[i];
        if(d->in_use && memcmp(d->f.mac, mac, 6) == 0) {
            bl_scanner_fill_view(s, d, out);
            found = true;
            break;
        }
    }

    furi_mutex_release(s->mutex);
    return found;
}

bool bl_scanner_get_features(BlScanner* s, const uint8_t mac[6], BlFeatures* out) {
    bool found = false;
    furi_mutex_acquire(s->mutex, FuriWaitForever);

    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        const BlDeviceSlot* d = &s->devices[i];
        if(d->in_use && memcmp(d->f.mac, mac, 6) == 0) {
            *out = d->f;
            found = true;
            break;
        }
    }

    furi_mutex_release(s->mutex);
    return found;
}

void bl_scanner_set_captured(BlScanner* s, const uint8_t mac[6], bool captured) {
    furi_mutex_acquire(s->mutex, FuriWaitForever);

    for(size_t i = 0; i < BL_DEV_MAX; i++) {
        BlDeviceSlot* d = &s->devices[i];
        if(d->in_use && memcmp(d->f.mac, mac, 6) == 0) {
            d->captured = captured;
            break;
        }
    }

    furi_mutex_release(s->mutex);
}

void bl_scanner_get_stats(BlScanner* s, BlScanStats* out) {
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    *out = s->stats;
    furi_mutex_release(s->mutex);
}

bool bl_scanner_log_start(BlScanner* s, const char* path) {
    if(s->log_active) return true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    s->log_stream = buffered_file_stream_alloc(storage);

    bool ok = buffered_file_stream_open(s->log_stream, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        FuriString* header =
            furi_string_alloc_set_str("tick_ms,mac,addr_type,event_type,rssi,adv_hex,name,score\n");
        stream_write_string(s->log_stream, header);
        furi_string_free(header);

        furi_mutex_acquire(s->mutex, FuriWaitForever);
        s->log_active = true;
        furi_mutex_release(s->mutex);
    } else {
        FURI_LOG_E(TAG, "Cannot open log %s", path);
        stream_free(s->log_stream);
        s->log_stream = NULL;
    }

    furi_record_close(RECORD_STORAGE);
    return ok;
}

void bl_scanner_log_stop(BlScanner* s) {
    if(!s->log_stream) return;

    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->log_active = false;
    furi_mutex_release(s->mutex);

    buffered_file_stream_close(s->log_stream);
    stream_free(s->log_stream);
    s->log_stream = NULL;
}

bool bl_scanner_log_is_active(const BlScanner* s) {
    return s->log_active;
}
