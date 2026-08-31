#include "bl_members.h"

#include <furi.h>
#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "BlMembers"

void bl_members_path(const char* folder, uint16_t id, char* out, size_t out_size) {
    snprintf(out, out_size, "%s/%u.txt", folder, id);
}

/* One member is written as the advertisement it arrived as. Everything the
 * matcher uses is derived from these bytes by bl_features_apply(). */
static void bl_members_write(Stream* stream, const BlFeatures* f) {
    FuriString* line = furi_string_alloc();
    char hex[BL_RAW_MAX * 2 + 1];

    furi_string_printf(
        line,
        "[member]\nmac=%02X%02X%02X%02X%02X%02X\naddrtype=%u\nconnectable=%u\n",
        f->mac[0],
        f->mac[1],
        f->mac[2],
        f->mac[3],
        f->mac[4],
        f->mac[5],
        f->addr_type,
        f->connectable ? 1 : 0);

    bl_hex_to_string(f->raw_adv, f->raw_adv_len, hex, sizeof(hex), false);
    furi_string_cat_printf(line, "adv=%s\n", hex);

    if(f->raw_rsp_len) {
        bl_hex_to_string(f->raw_rsp, f->raw_rsp_len, hex, sizeof(hex), false);
        furi_string_cat_printf(line, "rsp=%s\n", hex);
    }

    furi_string_cat_str(line, "\n");
    stream_write_string(stream, line);
    furi_string_free(line);
}

bool bl_members_save(const BlCaptureSet* set, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    bool ok = false;

    if(file_stream_open(stream, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* header = furi_string_alloc_set_str(
            "# BLE Locator group members, v1\n"
            "# One advertisement per member; the group is rebuilt from these.\n\n");
        stream_write_string(stream, header);
        furi_string_free(header);

        for(uint8_t i = 0; i < set->count; i++) {
            bl_members_write(stream, &set->items[i]);
        }
        ok = true;
    } else {
        FURI_LOG_E(TAG, "Cannot write %s", path);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* Accumulates the key/value lines of one [member] block, then replays them
 * through the live parser so a stored member and a heard one cannot diverge. */
typedef struct {
    uint8_t mac[6]; /**< display order, as written */
    uint8_t addr_type;
    bool connectable;
    uint8_t adv[BL_RAW_MAX];
    uint8_t adv_len;
    uint8_t rsp[BL_RAW_MAX];
    uint8_t rsp_len;
    bool has_adv;
} BlMemberRaw;

static void bl_members_finish(const BlMemberRaw* raw, BlCaptureSet* set) {
    if(!raw->has_adv || set->count >= BL_CAPTURE_MAX) return;

    BlFeatures* f = &set->items[set->count];
    bl_features_reset(f);

    FuriHalBtAdvReport report = {
        .event_type = raw->connectable ? FuriHalBtAdvEventTypeConnectableUndirected :
                                         FuriHalBtAdvEventTypeNonConnectableUndirected,
        .address_type = raw->addr_type,
        .rssi = 0,
        .data_len = raw->adv_len,
        .data = raw->adv,
    };
    /* bl_features_apply() expects HCI order, which is the reverse of how the
     * MAC is written out for humans. */
    for(uint8_t i = 0; i < 6; i++) report.address[i] = raw->mac[5 - i];
    bl_features_apply(f, &report);

    if(raw->rsp_len) {
        report.event_type = FuriHalBtAdvEventTypeScanResponse;
        report.data_len = raw->rsp_len;
        report.data = raw->rsp;
        bl_features_apply(f, &report);
    }

    set->count++;
}

bool bl_members_load(BlCaptureSet* set, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    bool opened = false;

    set->count = 0;

    if(file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        opened = true;
        FuriString* line = furi_string_alloc();
        BlMemberRaw raw;
        bool in_member = false;

        memset(&raw, 0, sizeof(raw));

        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            const char* text = furi_string_get_cstr(line);

            if(text[0] == '#' || text[0] == '\0') continue;

            if(!strcmp(text, "[member]")) {
                if(in_member) bl_members_finish(&raw, set);
                memset(&raw, 0, sizeof(raw));
                in_member = true;
                continue;
            }

            if(!in_member) continue;

            const char* eq = strchr(text, '=');
            if(!eq) continue;

            const size_t key_len = (size_t)(eq - text);
            const char* value = eq + 1;

            if(key_len == 3 && !strncmp(text, "mac", 3)) {
                bl_hex_from_string(value, raw.mac, sizeof(raw.mac));
            } else if(key_len == 8 && !strncmp(text, "addrtype", 8)) {
                raw.addr_type = (uint8_t)atoi(value);
            } else if(key_len == 11 && !strncmp(text, "connectable", 11)) {
                raw.connectable = (atoi(value) != 0);
            } else if(key_len == 3 && !strncmp(text, "adv", 3)) {
                raw.adv_len = (uint8_t)bl_hex_from_string(value, raw.adv, sizeof(raw.adv));
                raw.has_adv = true;
            } else if(key_len == 3 && !strncmp(text, "rsp", 3)) {
                raw.rsp_len = (uint8_t)bl_hex_from_string(value, raw.rsp, sizeof(raw.rsp));
            }
        }

        if(in_member) bl_members_finish(&raw, set);

        furi_string_free(line);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return opened;
}

void bl_members_remove(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, path);
    furi_record_close(RECORD_STORAGE);
}
