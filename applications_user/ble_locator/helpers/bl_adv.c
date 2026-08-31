#include "bl_adv.h"

#include <string.h>
#include <stdio.h>

void bl_features_reset(BlFeatures* f) {
    memset(f, 0, sizeof(BlFeatures));
}

static void bl_features_add_uuid16(BlFeatures* f, uint16_t uuid) {
    for(uint8_t i = 0; i < f->uuid16_count; i++) {
        if(f->uuid16[i] == uuid) return;
    }
    if(f->uuid16_count < BL_UUID16_MAX) {
        f->uuid16[f->uuid16_count++] = uuid;
    }
}

/* Parses one AD blob (either the ADV payload or a SCAN_RSP payload) */
static bool bl_features_parse_ad(BlFeatures* f, const uint8_t* data, uint8_t len) {
    bool changed = false;
    uint8_t i = 0;

    while(i < len) {
        const uint8_t field_len = data[i];
        /* A zero length terminates the AD list (padding) */
        if(field_len == 0) break;
        /* Truncated field - the rest of the payload is unusable */
        if((uint16_t)i + 1 + field_len > len) break;

        const uint8_t type = data[i + 1];
        const uint8_t* value = &data[i + 2];
        const uint8_t value_len = field_len - 1;

        const uint32_t type_bit = (type < 31) ? (1UL << type) : (1UL << 31);
        if(!(f->adtype_mask & type_bit)) {
            f->adtype_mask |= type_bit;
            changed = true;
        }

        switch(type) {
        case BL_AD_FLAGS:
            if(value_len >= 1 && !f->has_flags) {
                f->flags = value[0];
                f->has_flags = true;
                changed = true;
            }
            break;

        case BL_AD_UUID16_MORE:
        case BL_AD_UUID16_ALL:
            for(uint8_t p = 0; p + 1 < value_len; p += 2) {
                const uint8_t before = f->uuid16_count;
                bl_features_add_uuid16(f, (uint16_t)value[p] | ((uint16_t)value[p + 1] << 8));
                if(f->uuid16_count != before) changed = true;
            }
            break;

        case BL_AD_UUID128_MORE:
        case BL_AD_UUID128_ALL:
            if(value_len >= 16 && !f->has_uuid128) {
                /* Stored in advertising (little-endian) order */
                memcpy(f->uuid128, value, 16);
                f->has_uuid128 = true;
                changed = true;
            }
            break;

        case BL_AD_NAME_SHORT:
        case BL_AD_NAME_COMPLETE: {
            /* A complete name always wins over a shortened one */
            const bool better = (type == BL_AD_NAME_COMPLETE) || (f->name_len == 0);
            if(value_len > 0 && better) {
                uint8_t copy = value_len;
                if(copy > BL_NAME_MAX) copy = BL_NAME_MAX;
                if(copy != f->name_len || memcmp(f->name, value, copy) != 0) {
                    memcpy(f->name, value, copy);
                    f->name[copy] = '\0';
                    /* Strip anything unprintable so it is safe to draw */
                    for(uint8_t p = 0; p < copy; p++) {
                        if((uint8_t)f->name[p] < 0x20 || (uint8_t)f->name[p] > 0x7E) {
                            f->name[p] = '.';
                        }
                    }
                    f->name_len = copy;
                    changed = true;
                }
            }
        } break;

        case BL_AD_TX_POWER:
            if(value_len >= 1 && !f->has_tx_power) {
                f->tx_power = (int8_t)value[0];
                f->has_tx_power = true;
                changed = true;
            }
            break;

        case BL_AD_APPEARANCE:
            if(value_len >= 2 && !f->has_appearance) {
                f->appearance = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
                f->has_appearance = true;
                changed = true;
            }
            break;

        case BL_AD_SVC_DATA_UUID16:
            if(value_len >= 2 && !f->has_svc_data) {
                f->svc_data_uuid16 = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
                f->has_svc_data = true;
                /* Service data UUIDs are as identifying as advertised ones */
                bl_features_add_uuid16(f, f->svc_data_uuid16);
                changed = true;
            }
            break;

        case BL_AD_MANUFACTURER:
            if(value_len >= 2 && !f->has_company) {
                f->company_id = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
                f->has_company = true;
                f->mfg_len = value_len;
                uint8_t head = value_len - 2;
                if(head > BL_MFG_HEAD_MAX) head = BL_MFG_HEAD_MAX;
                memcpy(f->mfg_head, &value[2], head);
                f->mfg_head_len = head;
                changed = true;
            }
            break;

        default:
            break;
        }

        i += 1 + field_len;
    }

    return changed;
}

bool bl_features_apply(BlFeatures* f, const FuriHalBtAdvReport* report) {
    bool changed = false;

    /* HCI hands the address over least significant byte first */
    for(uint8_t i = 0; i < 6; i++) {
        f->mac[i] = report->address[5 - i];
    }
    f->addr_type = report->address_type;

    uint8_t len = report->data_len;
    if(len > BL_RAW_MAX) len = BL_RAW_MAX;

    if(report->event_type == FuriHalBtAdvEventTypeScanResponse) {
        if(len != f->raw_rsp_len || memcmp(f->raw_rsp, report->data, len) != 0) {
            memcpy(f->raw_rsp, report->data, len);
            f->raw_rsp_len = len;
            changed = true;
        }
        if(!f->had_scan_rsp) {
            f->had_scan_rsp = true;
            changed = true;
        }
    } else {
        if(len != f->raw_adv_len || memcmp(f->raw_adv, report->data, len) != 0) {
            memcpy(f->raw_adv, report->data, len);
            f->raw_adv_len = len;
            changed = true;
        }
        const bool connectable =
            (report->event_type == FuriHalBtAdvEventTypeConnectableUndirected) ||
            (report->event_type == FuriHalBtAdvEventTypeConnectableDirected);
        if(connectable != f->connectable) {
            f->connectable = connectable;
            changed = true;
        }
    }

    if(len > 0) {
        changed |= bl_features_parse_ad(f, report->data, len);
    }

    return changed;
}

void bl_mac_to_string(const uint8_t mac[6], char* out, size_t out_size) {
    snprintf(
        out,
        out_size,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
}

static int bl_hex_digit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool bl_oui_from_string(const char* str, uint8_t out[3]) {
    uint8_t tmp[3];
    if(bl_hex_from_string(str, tmp, 3) != 3) return false;
    memcpy(out, tmp, 3);
    return true;
}

void bl_hex_to_string(const uint8_t* data, size_t len, char* out, size_t out_size, bool spaced) {
    if(out_size == 0) return;
    out[0] = '\0';

    size_t pos = 0;
    static const char* digits = "0123456789ABCDEF";

    for(size_t i = 0; i < len; i++) {
        const size_t need = (spaced && pos > 0) ? 3 : 2;
        if(pos + need + 1 > out_size) break;
        if(spaced && pos > 0) out[pos++] = ' ';
        out[pos++] = digits[(data[i] >> 4) & 0x0F];
        out[pos++] = digits[data[i] & 0x0F];
    }

    out[pos] = '\0';
}

size_t bl_hex_from_string(const char* str, uint8_t* out, size_t out_size) {
    size_t written = 0;
    int hi = -1;

    for(const char* p = str; *p && written < out_size; p++) {
        const int digit = bl_hex_digit(*p);
        if(digit < 0) {
            /* separators reset the nibble pairing */
            hi = -1;
            continue;
        }
        if(hi < 0) {
            hi = digit;
        } else {
            out[written++] = (uint8_t)((hi << 4) | digit);
            hi = -1;
        }
    }

    return written;
}

void bl_features_describe(const BlFeatures* f, FuriString* out) {
    char buf[100];

    furi_string_reset(out);


    bl_mac_to_string(f->mac, buf, sizeof(buf));
    furi_string_cat_printf(out, "MAC  %s\n", buf);
    furi_string_cat_printf(
        out,
        "Addr %s, %s\n",
        (f->addr_type == 0 || f->addr_type == 2) ? "public" : "random",
        f->connectable ? "connectable" : "beacon");

    if(f->name_len) {
        furi_string_cat_printf(out, "Name \"%s\"\n", f->name);
    } else {
        furi_string_cat_str(out, "Name (none)\n");
    }

    if(f->has_company) {
        furi_string_cat_printf(out, "Mfr  0x%04X (%u B)\n", f->company_id, f->mfg_len);
        bl_hex_to_string(f->mfg_head, f->mfg_head_len, buf, sizeof(buf), true);
        furi_string_cat_printf(out, "Data %s\n", buf);
    }

    if(f->uuid16_count) {
        furi_string_cat_str(out, "UUID16");
        for(uint8_t i = 0; i < f->uuid16_count; i++) {
            furi_string_cat_printf(out, " %04X", f->uuid16[i]);
        }
        furi_string_cat_str(out, "\n");
    }

    if(f->has_uuid128) {
        bl_hex_to_string(f->uuid128, 16, buf, sizeof(buf), false);
        furi_string_cat_printf(out, "UUID128 %s\n", buf);
    }

    if(f->has_flags) furi_string_cat_printf(out, "Flags 0x%02X\n", f->flags);
    if(f->has_tx_power) furi_string_cat_printf(out, "TxPwr %d dBm\n", f->tx_power);
    if(f->has_appearance) furi_string_cat_printf(out, "Appear 0x%04X\n", f->appearance);

    bl_hex_to_string(f->raw_adv, f->raw_adv_len, buf, sizeof(buf), true);
    furi_string_cat_printf(out, "\nADV (%u)\n%s\n", f->raw_adv_len, buf);

    if(f->raw_rsp_len) {
        bl_hex_to_string(f->raw_rsp, f->raw_rsp_len, buf, sizeof(buf), true);
        furi_string_cat_printf(out, "RSP (%u)\n%s\n", f->raw_rsp_len, buf);
    }
}

void bl_features_label(const BlFeatures* f, char* out, size_t out_size) {
    if(f->name_len > 0) {
        snprintf(out, out_size, "%s", f->name);
    } else {
        snprintf(
            out, out_size, "%02X%02X%02X%02X%02X%02X", f->mac[0], f->mac[1], f->mac[2], f->mac[3],
            f->mac[4], f->mac[5]);
    }
}
