#include "bl_group.h"

#include <storage/storage.h>
#include <toolbox/stream/stream.h>
#include <toolbox/stream/file_stream.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "BlGroup"

/* Weights are relative; the final score is normalised to 0..100 so a group
 * that only knows two things is not automatically weaker than one that knows
 * six. What keeps weak groups honest is bl_group_build() refusing to
 * create a fingerprint without at least one strong criterion. */
#define W_OUI         20
#define W_COMPANY     25
#define W_MFGHEAD_PER 6 /* per byte of common manufacturer-data prefix */
#define W_MFGHEAD_MAX 30
#define W_UUID128     30
#define W_UUID16      20
#define W_NAME_PER    4 /* per character of common name prefix */
#define W_NAME_MAX    30
#define W_NONAME      8
/* Structural traits are cheap for an unrelated device to satisfy by accident,
 * so they are worth little on their own. */
#define W_ADTYPES     8
#define W_ADVLEN      4
#define W_MFGLEN      4
#define W_ADDRTYPE    2
#define W_CONNECTABLE 2

#define BL_DEFAULT_THRESHOLD 55

static bool bl_features_has_uuid16(const BlFeatures* f, uint16_t uuid) {
    for(uint8_t i = 0; i < f->uuid16_count; i++) {
        if(f->uuid16[i] == uuid) return true;
    }
    return false;
}

static uint8_t bl_adv_payload_len(const BlFeatures* f) {
    return f->raw_adv_len;
}

static uint8_t bl_ci_prefix_match(const char* text, const char* prefix, uint8_t prefix_len) {
    for(uint8_t i = 0; i < prefix_len; i++) {
        char a = text[i];
        char b = prefix[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if(b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if(a != b) return 0;
    }
    return 1;
}

/* Scoring rules
 *
 * Every criterion the group asserts contributes its weight to the maximum,
 * and the result is that fraction expressed as a percentage.
 *
 * Three things can veto a device outright: a manufacturer id, a 128-bit
 * service UUID or a device name that is present but *different* from what the
 * group expects. Everything else is graded. The distinction that matters is
 * between contradiction and absence: a device whose name only rides in a scan
 * response is invisible during a passive scan, and losing it because a field
 * happened to be missing would be a bug, not caution.
 *
 * Finally, at least one identifying criterion has to actually hit. Matching
 * only on shape (payload lengths, AD types, address kind) says nothing.
 */
int bl_group_score(const BlGroup* p, const BlFeatures* f) {
    int total = 0;
    int hit = 0;
    bool strong_asserted = false;
    bool strong_hit = false;

    if(p->crit & BlCritCompany) {
        total += W_COMPANY;
        strong_asserted = true;
        if(f->has_company) {
            if(f->company_id != p->company_id) return -1;
            hit += W_COMPANY;
            strong_hit = true;
        }
    }

    if(p->crit & BlCritMfgHead) {
        int w = p->mfg_head_len * W_MFGHEAD_PER;
        if(w > W_MFGHEAD_MAX) w = W_MFGHEAD_MAX;
        total += w;
        strong_asserted = true;

        /* Graded: a learned prefix can accidentally reach into a serial number,
         * so demanding all of it would reject the rest of the group. */
        if(f->mfg_head_len > 0) {
            uint8_t matched = 0;
            const uint8_t limit =
                (p->mfg_head_len < f->mfg_head_len) ? p->mfg_head_len : f->mfg_head_len;
            while(matched < limit && p->mfg_head[matched] == f->mfg_head[matched]) matched++;

            if(matched > 0) {
                hit += (w * matched) / p->mfg_head_len;
                if(matched >= 2) strong_hit = true;
            }
        }
    }

    if(p->crit & BlCritUuid128) {
        total += W_UUID128;
        strong_asserted = true;
        if(f->has_uuid128) {
            if(memcmp(f->uuid128, p->uuid128, 16) != 0) return -1;
            hit += W_UUID128;
            strong_hit = true;
        }
    }

    if(p->crit & BlCritNamePrefix) {
        int w = p->name_prefix_len * W_NAME_PER;
        if(w > W_NAME_MAX) w = W_NAME_MAX;
        total += w;
        strong_asserted = true;
        if(f->name_len > 0) {
            if(f->name_len < p->name_prefix_len) return -1;
            if(!bl_ci_prefix_match(f->name, p->name_prefix, p->name_prefix_len)) return -1;
            hit += w;
            strong_hit = true;
        }
    }

    if(p->crit & BlCritUuid16) {
        total += W_UUID16;
        strong_asserted = true;
        if(p->uuid16_count > 0) {
            uint8_t found = 0;
            for(uint8_t i = 0; i < p->uuid16_count; i++) {
                if(bl_features_has_uuid16(f, p->uuid16[i])) found++;
            }
            if(found > 0) {
                hit += (W_UUID16 * found) / p->uuid16_count;
                if(found == p->uuid16_count) strong_hit = true;
            }
        }
    }

    if(p->crit & BlCritNoName) {
        total += W_NONAME;
        if(f->name_len == 0) hit += W_NONAME;
    }

    if(p->crit & BlCritOui) {
        total += W_OUI;
        strong_asserted = true;
        if(memcmp(f->mac, p->oui, 3) == 0) {
            hit += W_OUI;
            strong_hit = true;
        }
    }

    if(p->crit & BlCritAdTypes) {
        total += W_ADTYPES;
        if((f->adtype_mask & p->adtype_mask) == p->adtype_mask) hit += W_ADTYPES;
    }

    if(p->crit & BlCritAdvLen) {
        total += W_ADVLEN;
        const uint8_t len = bl_adv_payload_len(f);
        /* one byte of slack: groups often carry a variable-length id */
        if(len + 1 >= p->adv_len_min && len <= (uint8_t)(p->adv_len_max + 1)) hit += W_ADVLEN;
    }

    if(p->crit & BlCritMfgLen) {
        total += W_MFGLEN;
        if(f->mfg_len >= p->mfg_len_min && f->mfg_len <= p->mfg_len_max) hit += W_MFGLEN;
    }

    if(p->crit & BlCritAddrType) {
        total += W_ADDRTYPE;
        if(f->addr_type == p->addr_type) hit += W_ADDRTYPE;
    }

    if(p->crit & BlCritConnectable) {
        total += W_CONNECTABLE;
        if(f->connectable == p->connectable) hit += W_CONNECTABLE;
    }

    if(total == 0) return -1;
    if(strong_asserted && !strong_hit) return 0;

    return (hit * 100) / total;
}

/* --- learning ----------------------------------------------------------- */

static uint8_t bl_common_prefix_len(const uint8_t* a, uint8_t a_len, const uint8_t* b, uint8_t b_len) {
    const uint8_t max = (a_len < b_len) ? a_len : b_len;
    uint8_t i = 0;
    while(i < max && a[i] == b[i]) i++;
    return i;
}

/* A group id is usually the trailing digits of the name, so a prefix that ends
 * mid-number is not useful - back off to the last non-alphanumeric boundary or
 * to the start of the digit run. */
static uint8_t bl_trim_name_prefix(const char* name, uint8_t len) {
    while(len > 0) {
        const char c = name[len - 1];
        if(c >= '0' && c <= '9') {
            len--;
            continue;
        }
        break;
    }
    return len;
}

bool bl_group_build(const BlCaptureSet* set, BlGroup* out, const char** error) {
    const char* dummy = NULL;
    if(!error) error = &dummy;
    *error = NULL;

    if(!set || set->count == 0) {
        *error = "No captures";
        return false;
    }

    memset(out, 0, sizeof(BlGroup));
    out->enabled = true;
    out->captures = set->count;
    out->threshold = BL_DEFAULT_THRESHOLD;

    const BlFeatures* first = &set->items[0];

    /* --- OUI ------------------------------------------------------------ */
    bool same_oui = true;
    for(uint8_t i = 1; i < set->count; i++) {
        if(memcmp(set->items[i].mac, first->mac, 3) != 0) {
            same_oui = false;
            break;
        }
    }
    if(same_oui) {
        memcpy(out->oui, first->mac, 3);
        out->crit |= BlCritOui;
    }

    /* --- manufacturer data ---------------------------------------------- */
    bool all_company = first->has_company;
    for(uint8_t i = 1; i < set->count && all_company; i++) {
        all_company = set->items[i].has_company && (set->items[i].company_id == first->company_id);
    }
    if(all_company) {
        out->company_id = first->company_id;
        out->crit |= BlCritCompany;

        uint8_t head = first->mfg_head_len;
        for(uint8_t i = 1; i < set->count; i++) {
            head = bl_common_prefix_len(
                first->mfg_head, head, set->items[i].mfg_head, set->items[i].mfg_head_len);
        }
        /* One capture cannot tell a protocol header from a serial number, so
         * trust only as many bytes as the evidence supports. */
        const uint8_t head_cap = (uint8_t)(2 + set->count);
        if(head > head_cap) head = head_cap;

        if(head > 0) {
            memcpy(out->mfg_head, first->mfg_head, head);
            out->mfg_head_len = head;
            out->crit |= BlCritMfgHead;
        }

        out->mfg_len_min = out->mfg_len_max = first->mfg_len;
        for(uint8_t i = 1; i < set->count; i++) {
            const uint8_t l = set->items[i].mfg_len;
            if(l < out->mfg_len_min) out->mfg_len_min = l;
            if(l > out->mfg_len_max) out->mfg_len_max = l;
        }
        out->crit |= BlCritMfgLen;
    }

    /* --- 128-bit service UUID ------------------------------------------- */
    bool all_uuid128 = first->has_uuid128;
    for(uint8_t i = 1; i < set->count && all_uuid128; i++) {
        all_uuid128 = set->items[i].has_uuid128 &&
                      (memcmp(set->items[i].uuid128, first->uuid128, 16) == 0);
    }
    if(all_uuid128) {
        memcpy(out->uuid128, first->uuid128, 16);
        out->crit |= BlCritUuid128;
    }

    /* --- 16-bit service UUIDs (intersection) ----------------------------- */
    for(uint8_t u = 0; u < first->uuid16_count; u++) {
        const uint16_t uuid = first->uuid16[u];
        bool in_all = true;
        for(uint8_t i = 1; i < set->count; i++) {
            if(!bl_features_has_uuid16(&set->items[i], uuid)) {
                in_all = false;
                break;
            }
        }
        if(in_all && out->uuid16_count < BL_UUID16_MAX) {
            out->uuid16[out->uuid16_count++] = uuid;
        }
    }
    if(out->uuid16_count > 0) out->crit |= BlCritUuid16;

    /* --- name ------------------------------------------------------------ */
    bool any_name = false;
    for(uint8_t i = 0; i < set->count; i++) {
        if(set->items[i].name_len > 0) any_name = true;
    }
    if(any_name) {
        uint8_t prefix = first->name_len;
        for(uint8_t i = 1; i < set->count; i++) {
            prefix = bl_common_prefix_len(
                (const uint8_t*)first->name,
                prefix,
                (const uint8_t*)set->items[i].name,
                set->items[i].name_len);
        }
        /* With a single capture, the whole name would become the prefix and the
         * group would only ever match that one device. Trim the id digits. */
        prefix = bl_trim_name_prefix(first->name, prefix);
        if(prefix >= 3) {
            memcpy(out->name_prefix, first->name, prefix);
            out->name_prefix[prefix] = '\0';
            out->name_prefix_len = prefix;
            out->crit |= BlCritNamePrefix;
        }
    } else {
        out->crit |= BlCritNoName;
    }

    /* --- structural shape ------------------------------------------------ */
    out->adtype_mask = first->adtype_mask;
    for(uint8_t i = 1; i < set->count; i++) {
        out->adtype_mask &= set->items[i].adtype_mask;
    }
    if(out->adtype_mask != 0) out->crit |= BlCritAdTypes;

    out->adv_len_min = out->adv_len_max = first->raw_adv_len;
    for(uint8_t i = 1; i < set->count; i++) {
        const uint8_t l = set->items[i].raw_adv_len;
        if(l < out->adv_len_min) out->adv_len_min = l;
        if(l > out->adv_len_max) out->adv_len_max = l;
    }
    if(out->adv_len_max > 0) out->crit |= BlCritAdvLen;

    bool same_addr_type = true;
    bool same_conn = true;
    for(uint8_t i = 1; i < set->count; i++) {
        if(set->items[i].addr_type != first->addr_type) same_addr_type = false;
        if(set->items[i].connectable != first->connectable) same_conn = false;
    }
    if(same_addr_type) {
        out->addr_type = first->addr_type;
        out->crit |= BlCritAddrType;
    }
    if(same_conn) {
        out->connectable = first->connectable;
        out->crit |= BlCritConnectable;
    }

    /* --- sanity: refuse fingerprints that would match half the street ---- */
    const uint32_t strong = BlCritCompany | BlCritUuid128 | BlCritUuid16 | BlCritNamePrefix |
                            BlCritMfgHead | BlCritOui;
    if((out->crit & strong) == 0) {
        *error = "Too generic - capture\nmore/other units";
        return false;
    }

    /* A lone OUI is weak evidence on its own: plenty of unrelated gear shares a
     * MAC block. Demand a second criterion in that case. */
    if((out->crit & strong) == BlCritOui && set->count < 2) {
        *error = "Only MAC vendor matched.\nCapture 2+ units";
        return false;
    }

    snprintf(out->name, sizeof(out->name), "Group");
    return true;
}

/* Leave this much daylight between the best impostor and the threshold. */
#define BL_TIGHTEN_MARGIN 5
/* Never demand more than this, or genuine group members that differ slightly
 * from the captured ones get shut out too. */
#define BL_TIGHTEN_CEILING 92
/* Nor less than this, whatever the surroundings suggest. */
#define BL_TIGHTEN_FLOOR 35

void bl_group_tighten(
    BlGroup* group,
    const BlCaptureSet* captures,
    const int8_t* negative_scores,
    size_t negative_count,
    BlBuildReport* report) {
    BlBuildReport local;
    if(!report) report = &local;

    memset(report, 0, sizeof(BlBuildReport));
    report->captures = captures ? captures->count : 0;
    report->others_tested = (uint16_t)negative_count;
    report->max_other_score = -1;

    /* By construction every criterion is derived to hold for all captures, so
     * they should all score 100 - but measure rather than assume. */
    int min_capture = 100;
    if(captures) {
        for(uint8_t i = 0; i < captures->count; i++) {
            const int score = bl_group_score(group, &captures->items[i]);
            if(score < min_capture) min_capture = score;
        }
    }
    if(min_capture < 0) min_capture = 0;
    report->min_capture_score = (uint8_t)min_capture;

    int max_negative = -1;
    for(size_t i = 0; i < negative_count; i++) {
        if(negative_scores[i] > max_negative) max_negative = negative_scores[i];
    }
    report->max_other_score = (int8_t)max_negative;

    int threshold;
    if(max_negative < 0) {
        /* Nothing else out there even soft-matches; keep the default so
         * group members that differ a little are still picked up. */
        threshold = group->threshold;
    } else {
        threshold = max_negative + BL_TIGHTEN_MARGIN;
    }

    if(threshold < BL_TIGHTEN_FLOOR) threshold = BL_TIGHTEN_FLOOR;
    if(threshold > BL_TIGHTEN_CEILING) threshold = BL_TIGHTEN_CEILING;
    /* Pointless to demand more than the captures themselves can score. */
    if(threshold > min_capture) threshold = min_capture;

    group->threshold = (uint8_t)threshold;

    uint16_t still_matching = 0;
    for(size_t i = 0; i < negative_count; i++) {
        if(negative_scores[i] >= 0 && negative_scores[i] >= threshold) still_matching++;
    }
    report->others_matched = still_matching;
    report->separated = (still_matching == 0);
}

void bl_group_describe(const BlGroup* p, char* out, size_t out_size) {
    char buf[64];
    buf[0] = '\0';
    size_t pos = 0;

    struct {
        uint32_t bit;
        const char* label;
    } const parts[] = {
        {BlCritNamePrefix, "name"},
        {BlCritCompany, "mfr"},
        {BlCritMfgHead, "data"},
        {BlCritUuid128, "uuid128"},
        {BlCritUuid16, "uuid16"},
        {BlCritOui, "oui"},
        {BlCritNoName, "silent"},
        {BlCritAdTypes, "shape"},
    };

    for(size_t i = 0; i < COUNT_OF(parts); i++) {
        if(!(p->crit & parts[i].bit)) continue;
        const int written = snprintf(
            &buf[pos], sizeof(buf) - pos, "%s%s", (pos > 0) ? "+" : "", parts[i].label);
        if(written <= 0 || (size_t)written >= sizeof(buf) - pos) break;
        pos += (size_t)written;
    }

    snprintf(out, out_size, "%s", buf[0] ? buf : "empty");
}

void bl_group_details(const BlGroup* p, FuriString* out) {
    char buf[80];

    furi_string_printf(out, "%s\n", p->name);
    furi_string_cat_printf(out, "learned, %u capture(s)\n", p->captures);
    furi_string_cat_printf(out, "Threshold: %u%%\n\n", p->threshold);

    if(p->crit & BlCritNamePrefix) {
        furi_string_cat_printf(out, "Name starts: \"%s\"\n", p->name_prefix);
    }
    if(p->crit & BlCritNoName) {
        furi_string_cat_str(out, "No device name\n");
    }
    if(p->crit & BlCritCompany) {
        furi_string_cat_printf(out, "Company ID: 0x%04X\n", p->company_id);
    }
    if(p->crit & BlCritMfgHead) {
        bl_hex_to_string(p->mfg_head, p->mfg_head_len, buf, sizeof(buf), true);
        furi_string_cat_printf(out, "Mfg starts: %s\n", buf);
    }
    if(p->crit & BlCritMfgLen) {
        furi_string_cat_printf(out, "Mfg len: %u-%u\n", p->mfg_len_min, p->mfg_len_max);
    }
    if(p->crit & BlCritUuid128) {
        bl_hex_to_string(p->uuid128, 16, buf, sizeof(buf), false);
        furi_string_cat_printf(out, "UUID128: %s\n", buf);
    }
    if(p->crit & BlCritUuid16) {
        furi_string_cat_str(out, "UUID16:");
        for(uint8_t i = 0; i < p->uuid16_count; i++) {
            furi_string_cat_printf(out, " %04X", p->uuid16[i]);
        }
        furi_string_cat_str(out, "\n");
    }
    if(p->crit & BlCritOui) {
        furi_string_cat_printf(
            out, "MAC vendor: %02X:%02X:%02X\n", p->oui[0], p->oui[1], p->oui[2]);
    }
    if(p->crit & BlCritAdvLen) {
        furi_string_cat_printf(out, "Adv len: %u-%u\n", p->adv_len_min, p->adv_len_max);
    }
    if(p->crit & BlCritAdTypes) {
        furi_string_cat_printf(out, "AD types: 0x%08lX\n", (unsigned long)p->adtype_mask);
    }
    if(p->crit & BlCritAddrType) {
        furi_string_cat_printf(
            out, "Addr type: %s\n", p->addr_type == 0 ? "public" : "random");
    }
    if(p->crit & BlCritConnectable) {
        furi_string_cat_printf(out, "Connectable: %s\n", p->connectable ? "yes" : "no");
    }
}

/* --- list helpers -------------------------------------------------------- */

bool bl_group_list_add(BlGroupList* list, const BlGroup* p) {
    for(uint8_t i = 0; i < list->count; i++) {
        if(strncmp(list->items[i].name, p->name, BL_GROUP_NAME_MAX) == 0) {
            list->items[i] = *p;
            return true;
        }
    }
    if(list->count >= BL_GROUP_MAX) return false;
    list->items[list->count++] = *p;
    return true;
}

void bl_group_list_remove(BlGroupList* list, uint8_t index) {
    if(index >= list->count) return;
    for(uint8_t i = index; i + 1 < list->count; i++) {
        list->items[i] = list->items[i + 1];
    }
    list->count--;
}

void bl_group_name_trim(char* name) {
    size_t len = strlen(name);
    while(len > 0 && name[len - 1] == ' ') {
        name[--len] = '\0';
    }
    size_t lead = 0;
    while(name[lead] == ' ') {
        lead++;
    }
    if(lead) memmove(name, name + lead, len - lead + 1);
}

int8_t bl_group_list_find(const BlGroupList* list, const char* name) {
    for(uint8_t i = 0; i < list->count; i++) {
        if(strncmp(list->items[i].name, name, BL_GROUP_NAME_MAX) == 0) return (int8_t)i;
    }
    return -1;
}

const char* bl_group_name_problem(const BlGroupList* list, const char* name, int8_t self) {
    char clean[BL_GROUP_NAME_MAX + 1];
    snprintf(clean, sizeof(clean), "%s", name);
    bl_group_name_trim(clean);
    if(clean[0] == '\0') return "Name is\nempty";

    int8_t other = bl_group_list_find(list, clean);
    if(other >= 0 && other != self) return "Name already\nused";
    return NULL;
}

bool bl_group_name_available(const BlGroupList* list, const char* name, int8_t self) {
    return bl_group_name_problem(list, name, self) == NULL;
}

uint16_t bl_group_list_next_id(const BlGroupList* list) {
    uint16_t max = 0;
    for(uint8_t i = 0; i < list->count; i++) {
        if(list->items[i].id > max) max = list->items[i].id;
    }
    return (uint16_t)(max + 1);
}

void bl_group_list_assign_ids(BlGroupList* list) {
    for(uint8_t i = 0; i < list->count; i++) {
        /* 0 means unassigned; a duplicate would make two groups share a member
         * file, so it gets a fresh one too. */
        bool clash = (list->items[i].id == 0);
        for(uint8_t j = 0; j < i && !clash; j++) {
            if(list->items[j].id == list->items[i].id) clash = true;
        }
        if(clash) list->items[i].id = bl_group_list_next_id(list);
    }
}

/* --- persistence --------------------------------------------------------- */

static void bl_group_write(Stream* stream, const BlGroup* p) {
    char buf[80];
    FuriString* line = furi_string_alloc();

    furi_string_printf(line, "[group]\n");
    furi_string_cat_printf(line, "name=%s\n", p->name);
    furi_string_cat_printf(line, "id=%u\n", p->id);
    furi_string_cat_printf(line, "enabled=%u\n", p->enabled ? 1 : 0);
    furi_string_cat_printf(line, "builtin=%u\n", p->builtin ? 1 : 0);
    furi_string_cat_printf(line, "captures=%u\n", p->captures);
    furi_string_cat_printf(line, "threshold=%u\n", p->threshold);
    furi_string_cat_printf(line, "crit=%08lX\n", (unsigned long)p->crit);

    if(p->crit & BlCritOui) {
        furi_string_cat_printf(
            line, "oui=%02X%02X%02X\n", p->oui[0], p->oui[1], p->oui[2]);
    }
    if(p->crit & BlCritCompany) {
        furi_string_cat_printf(line, "company=%04X\n", p->company_id);
    }
    if(p->crit & BlCritMfgHead) {
        bl_hex_to_string(p->mfg_head, p->mfg_head_len, buf, sizeof(buf), false);
        furi_string_cat_printf(line, "mfghead=%s\n", buf);
    }
    if(p->crit & BlCritUuid128) {
        bl_hex_to_string(p->uuid128, 16, buf, sizeof(buf), false);
        furi_string_cat_printf(line, "uuid128=%s\n", buf);
    }
    if(p->crit & BlCritUuid16) {
        furi_string_cat_str(line, "uuid16=");
        for(uint8_t i = 0; i < p->uuid16_count; i++) {
            furi_string_cat_printf(line, "%s%04X", i ? "," : "", p->uuid16[i]);
        }
        furi_string_cat_str(line, "\n");
    }
    if(p->crit & BlCritNamePrefix) {
        furi_string_cat_printf(line, "nameprefix=%s\n", p->name_prefix);
    }
    if(p->crit & BlCritAdTypes) {
        furi_string_cat_printf(line, "adtypes=%08lX\n", (unsigned long)p->adtype_mask);
    }
    if(p->crit & BlCritAdvLen) {
        furi_string_cat_printf(line, "advlen=%u,%u\n", p->adv_len_min, p->adv_len_max);
    }
    if(p->crit & BlCritMfgLen) {
        furi_string_cat_printf(line, "mfglen=%u,%u\n", p->mfg_len_min, p->mfg_len_max);
    }
    if(p->crit & BlCritAddrType) {
        furi_string_cat_printf(line, "addrtype=%u\n", p->addr_type);
    }
    if(p->crit & BlCritConnectable) {
        furi_string_cat_printf(line, "connectable=%u\n", p->connectable ? 1 : 0);
    }
    furi_string_cat_str(line, "\n");

    stream_write_string(stream, line);
    furi_string_free(line);
}

bool bl_group_list_save(const BlGroupList* list, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    bool ok = false;

    if(file_stream_open(stream, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FuriString* header = furi_string_alloc_set_str(
            "# BLE Locator device groups, v1\n"
            "# Edit by hand if you like; unknown keys are ignored.\n\n");
        stream_write_string(stream, header);
        furi_string_free(header);

        for(uint8_t i = 0; i < list->count; i++) {
            bl_group_write(stream, &list->items[i]);
        }
        ok = true;
    } else {
        FURI_LOG_E(TAG, "Cannot write %s", path);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static void bl_group_apply_kv(BlGroup* p, const char* key, const char* value) {
    if(!strcmp(key, "name")) {
        snprintf(p->name, sizeof(p->name), "%s", value);
    } else if(!strcmp(key, "id")) {
        p->id = (uint16_t)atoi(value);
    } else if(!strcmp(key, "enabled")) {
        p->enabled = (atoi(value) != 0);
    } else if(!strcmp(key, "builtin")) {
        p->builtin = (atoi(value) != 0);
    } else if(!strcmp(key, "captures")) {
        p->captures = (uint8_t)atoi(value);
    } else if(!strcmp(key, "threshold")) {
        p->threshold = (uint8_t)atoi(value);
    } else if(!strcmp(key, "crit")) {
        p->crit = (uint32_t)strtoul(value, NULL, 16);
    } else if(!strcmp(key, "oui")) {
        bl_hex_from_string(value, p->oui, 3);
    } else if(!strcmp(key, "company")) {
        p->company_id = (uint16_t)strtoul(value, NULL, 16);
    } else if(!strcmp(key, "mfghead")) {
        p->mfg_head_len = (uint8_t)bl_hex_from_string(value, p->mfg_head, BL_MFG_HEAD_MAX);
    } else if(!strcmp(key, "uuid128")) {
        bl_hex_from_string(value, p->uuid128, 16);
    } else if(!strcmp(key, "uuid16")) {
        p->uuid16_count = 0;
        const char* cur = value;
        while(*cur && p->uuid16_count < BL_UUID16_MAX) {
            p->uuid16[p->uuid16_count++] = (uint16_t)strtoul(cur, NULL, 16);
            const char* comma = strchr(cur, ',');
            if(!comma) break;
            cur = comma + 1;
        }
    } else if(!strcmp(key, "nameprefix")) {
        snprintf(p->name_prefix, sizeof(p->name_prefix), "%s", value);
        p->name_prefix_len = (uint8_t)strlen(p->name_prefix);
    } else if(!strcmp(key, "adtypes")) {
        p->adtype_mask = (uint32_t)strtoul(value, NULL, 16);
    } else if(!strcmp(key, "advlen")) {
        p->adv_len_min = (uint8_t)atoi(value);
        const char* comma = strchr(value, ',');
        p->adv_len_max = comma ? (uint8_t)atoi(comma + 1) : p->adv_len_min;
    } else if(!strcmp(key, "mfglen")) {
        p->mfg_len_min = (uint8_t)atoi(value);
        const char* comma = strchr(value, ',');
        p->mfg_len_max = comma ? (uint8_t)atoi(comma + 1) : p->mfg_len_min;
    } else if(!strcmp(key, "addrtype")) {
        p->addr_type = (uint8_t)atoi(value);
    } else if(!strcmp(key, "connectable")) {
        p->connectable = (atoi(value) != 0);
    }
}

bool bl_group_list_load(BlGroupList* list, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    Stream* stream = file_stream_alloc(storage);
    bool opened = false;

    list->count = 0;

    if(file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        opened = true;
        FuriString* line = furi_string_alloc();
        BlGroup current;
        bool in_group = false;

        while(stream_read_line(stream, line)) {
            furi_string_trim(line);
            const char* text = furi_string_get_cstr(line);

            if(text[0] == '#' || text[0] == '\0') continue;

            /* "[profile]" is what the scooter-only version wrote. Still
             * accepted so an old file loads without hand-editing. */
            if(!strcmp(text, "[group]") || !strcmp(text, "[profile]")) {
                if(in_group && list->count < BL_GROUP_MAX) {
                    list->items[list->count++] = current;
                }
                memset(&current, 0, sizeof(current));
                current.enabled = true;
                current.threshold = BL_DEFAULT_THRESHOLD;
                in_group = true;
                continue;
            }

            if(!in_group) continue;

            const char* eq = strchr(text, '=');
            if(!eq) continue;

            char key[24];
            const size_t key_len = (size_t)(eq - text);
            if(key_len == 0 || key_len >= sizeof(key)) continue;
            memcpy(key, text, key_len);
            key[key_len] = '\0';

            bl_group_apply_kv(&current, key, eq + 1);
        }

        if(in_group && list->count < BL_GROUP_MAX) {
            list->items[list->count++] = current;
        }

        furi_string_free(line);
    }

    stream_free(stream);
    furi_record_close(RECORD_STORAGE);
    return opened;
}
