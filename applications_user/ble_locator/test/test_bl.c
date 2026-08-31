/* Host-side tests for the Device Locator detection engine. */

#include "helpers/bl_adv.h"
#include "helpers/bl_group.h"
#include "helpers/bl_members.h"
#include "helpers/bl_order.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                        \
    do {                                        \
        checks++;                               \
        if(!(cond)) {                           \
            failures++;                         \
            printf("  FAIL: ");                 \
            printf(__VA_ARGS__);                \
            printf("  (%s:%d)\n", __FILE__, __LINE__); \
        }                                       \
    } while(0)

/* --- advertisement builder ------------------------------------------------ */

typedef struct {
    uint8_t data[31];
    uint8_t len;
} Adv;

static void adv_reset(Adv* a) {
    a->len = 0;
}

static void adv_add(Adv* a, uint8_t type, const uint8_t* value, uint8_t value_len) {
    if(a->len + 2 + value_len > 31) {
        printf("  (adv overflow, test bug)\n");
        return;
    }
    a->data[a->len++] = value_len + 1;
    a->data[a->len++] = type;
    memcpy(&a->data[a->len], value, value_len);
    a->len += value_len;
}

static void adv_add_str(Adv* a, uint8_t type, const char* text) {
    adv_add(a, type, (const uint8_t*)text, (uint8_t)strlen(text));
}

static void feed(BlFeatures* f, const Adv* a, uint8_t event_type, const uint8_t mac_be[6]) {
    FuriHalBtAdvReport r;
    r.event_type = event_type;
    r.address_type = 0;
    for(int i = 0; i < 6; i++) r.address[i] = mac_be[5 - i]; /* HCI order is LSB first */
    r.rssi = -60;
    r.data_len = a->len;
    r.data = a->data;
    bl_features_apply(f, &r);
}

/* Builds an advert shaped like a typical OEM sharing module:
 * flags + 16-bit service uuid + manufacturer data (company + type + serial) + name */
static void make_device(BlFeatures* out, const char* name, uint16_t serial, const uint8_t mac[6]) {
    Adv adv;
    adv_reset(&adv);

    const uint8_t flags = 0x06;
    adv_add(&adv, BL_AD_FLAGS, &flags, 1);

    const uint8_t uuid16[2] = {0xE7, 0xFE}; /* 0xFEE7, little-endian on air */
    adv_add(&adv, BL_AD_UUID16_ALL, uuid16, 2);

    const uint8_t mfg[8] = {
        0x57,
        0x07, /* company id 0x0757 */
        0x11,
        0x22, /* group-wide constant header */
        (uint8_t)(serial >> 8),
        (uint8_t)(serial & 0xFF),
        0x00,
        0x01,
    };
    adv_add(&adv, BL_AD_MANUFACTURER, mfg, sizeof(mfg));

    bl_features_reset(out);
    feed(out, &adv, FuriHalBtAdvEventTypeConnectableUndirected, mac);

    /* the name arrives in the scan response, as it often does in practice */
    Adv rsp;
    adv_reset(&rsp);
    adv_add_str(&rsp, BL_AD_NAME_COMPLETE, name);
    feed(out, &rsp, FuriHalBtAdvEventTypeScanResponse, mac);
}

/* --- tests ---------------------------------------------------------------- */

static void test_parsing(void) {
    printf("parsing\n");

    const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0x01, 0x02, 0x03};
    BlFeatures f;
    make_device(&f, "VOI-104233", 0x1234, mac);

    CHECK(memcmp(f.mac, mac, 6) == 0, "mac not restored to display order\n");
    CHECK(strcmp(f.name, "VOI-104233") == 0, "name is '%s'\n", f.name);
    CHECK(f.has_company && f.company_id == 0x0757, "company id 0x%04X\n", f.company_id);
    CHECK(f.mfg_len == 8, "mfg_len %u\n", f.mfg_len);
    CHECK(f.mfg_head_len == 6, "mfg_head_len %u\n", f.mfg_head_len);
    CHECK(f.mfg_head[0] == 0x11 && f.mfg_head[1] == 0x22, "mfg head wrong\n");
    CHECK(f.uuid16_count == 1 && f.uuid16[0] == 0xFEE7, "uuid16 %u/%04X\n", f.uuid16_count, f.uuid16[0]);
    CHECK(f.has_flags && f.flags == 0x06, "flags\n");
    CHECK(f.connectable, "should be connectable\n");
    CHECK(f.had_scan_rsp, "scan response not merged\n");
    CHECK(f.raw_adv_len > 0 && f.raw_rsp_len > 0, "raw payloads not kept separately\n");
    CHECK(
        (f.adtype_mask & ~((1u << 10) | (1u << 3) | (1u << 1) | (1u << 9) | (1u << 31))) == 0,
        "unexpected AD type bits 0x%08lX\n", (unsigned long)f.adtype_mask);
    CHECK((f.adtype_mask & (1u << 31)) != 0, "high AD type bit missing\n");
    CHECK((f.adtype_mask & (1u << BL_AD_NAME_COMPLETE)) != 0, "name AD type missing\n");
}

static void test_truncated_input(void) {
    printf("malformed input\n");

    BlFeatures f;
    bl_features_reset(&f);

    /* length byte claims more data than the packet holds */
    uint8_t bad[] = {0x1F, 0x09, 'A', 'B'};
    FuriHalBtAdvReport r = {
        .event_type = 0,
        .address_type = 0,
        .rssi = -50,
        .data_len = sizeof(bad),
        .data = bad,
    };
    memset(r.address, 0, 6);
    bl_features_apply(&f, &r);
    CHECK(f.name_len == 0, "truncated AD field must be ignored, got '%s'\n", f.name);

    /* zero length terminates the list */
    uint8_t padded[] = {0x00, 0x09, 'X'};
    r.data = padded;
    r.data_len = sizeof(padded);
    bl_features_apply(&f, &r);
    CHECK(f.name_len == 0, "padding must terminate parsing\n");

    /* an empty payload must not read out of bounds */
    r.data = padded;
    r.data_len = 0;
    bl_features_apply(&f, &r);
    CHECK(f.name_len == 0, "empty payload\n");
}

static void test_learn_and_match(void) {
    printf("learning + matching\n");

    BlCaptureSet set;
    set.count = 0;

    const uint8_t macs[3][6] = {
        {0x30, 0xAE, 0xA4, 0x11, 0x22, 0x33},
        {0x30, 0xAE, 0xA4, 0x44, 0x55, 0x66},
        {0x30, 0xAE, 0xA4, 0x77, 0x88, 0x99},
    };
    make_device(&set.items[set.count++], "VOI-104233", 0x1001, macs[0]);
    make_device(&set.items[set.count++], "VOI-118874", 0x1002, macs[1]);
    make_device(&set.items[set.count++], "VOI-200019", 0x1003, macs[2]);

    BlGroup p;
    const char* error = NULL;
    CHECK(bl_group_build(&set, &p, &error), "build failed: %s\n", error ? error : "?");

    CHECK(p.crit & BlCritCompany, "company not learned\n");
    CHECK(p.crit & BlCritUuid16, "uuid16 not learned\n");
    CHECK(p.crit & BlCritOui, "shared OUI not learned\n");
    CHECK(p.crit & BlCritNamePrefix, "name prefix not learned\n");
    CHECK(
        strcmp(p.name_prefix, "VOI-") == 0, "name prefix is '%s', expected 'VOI-'\n", p.name_prefix);
    CHECK(p.mfg_head_len >= 2, "mfg prefix too short: %u\n", p.mfg_head_len);
    CHECK(p.mfg_head[0] == 0x11 && p.mfg_head[1] == 0x22, "wrong mfg prefix\n");

    /* a fourth device of the same group */
    const uint8_t mac4[6] = {0x30, 0xAE, 0xA4, 0xAB, 0xCD, 0xEF};
    BlFeatures same;
    make_device(&same, "VOI-999001", 0x2001, mac4);
    const int score_same = bl_group_score(&p, &same);
    CHECK(score_same >= p.threshold, "sibling device scored %d (threshold %u)\n", score_same, p.threshold);

    /* same group, different MAC block and slightly different shape */
    BlFeatures cousin;
    const uint8_t mac5[6] = {0xC8, 0x2B, 0x96, 0x01, 0x02, 0x03};
    make_device(&cousin, "VOI-777001", 0x3001, mac5);
    const int score_cousin = bl_group_score(&p, &cousin);
    CHECK(
        score_cousin >= p.threshold,
        "same group on another MAC block scored %d (threshold %u) - too strict\n",
        score_cousin,
        p.threshold);

    /* an unrelated device: different company, different name */
    BlFeatures phone;
    bl_features_reset(&phone);
    Adv adv;
    adv_reset(&adv);
    const uint8_t flags = 0x1A;
    adv_add(&adv, BL_AD_FLAGS, &flags, 1);
    const uint8_t apple[6] = {0x4C, 0x00, 0x10, 0x05, 0x0B, 0x1C};
    adv_add(&adv, BL_AD_MANUFACTURER, apple, sizeof(apple));
    const uint8_t phone_mac[6] = {0x5A, 0x11, 0x22, 0x33, 0x44, 0x55};
    feed(&phone, &adv, FuriHalBtAdvEventTypeConnectableUndirected, phone_mac);
    CHECK(bl_group_score(&p, &phone) < 0, "an iPhone advert must hard-fail\n");

    /* right company, wrong product: same manufacturer id but different payload */
    BlFeatures sibling_product;
    bl_features_reset(&sibling_product);
    adv_reset(&adv);
    adv_add(&adv, BL_AD_FLAGS, &flags, 1);
    const uint8_t other_mfg[6] = {0x57, 0x07, 0x99, 0x88, 0x00, 0x00};
    adv_add(&adv, BL_AD_MANUFACTURER, other_mfg, sizeof(other_mfg));
    feed(&sibling_product, &adv, FuriHalBtAdvEventTypeConnectableUndirected, macs[0]);
    const int score_other = bl_group_score(&p, &sibling_product);
    CHECK(
        score_other >= 0 && score_other < p.threshold,
        "same company + same MAC block but different payload scored %d, expected below %u\n",
        score_other,
        p.threshold);

    /* Passive scanning never yields a scan response, so the same device shows
     * up without a name. It must still be recognised. */
    BlFeatures passive;
    bl_features_reset(&passive);
    adv_reset(&adv);
    const uint8_t f2 = 0x06;
    adv_add(&adv, BL_AD_FLAGS, &f2, 1);
    const uint8_t uuid16[2] = {0xE7, 0xFE};
    adv_add(&adv, BL_AD_UUID16_ALL, uuid16, 2);
    const uint8_t mfg[8] = {0x57, 0x07, 0x11, 0x22, 0x10, 0x77, 0x00, 0x01};
    adv_add(&adv, BL_AD_MANUFACTURER, mfg, sizeof(mfg));
    feed(&passive, &adv, FuriHalBtAdvEventTypeConnectableUndirected, mac4);
    const int score_passive = bl_group_score(&p, &passive);
    CHECK(
        score_passive >= p.threshold,
        "nameless (passive scan) sibling scored %d, expected >= %u\n",
        score_passive,
        p.threshold);

    /* A device that advertises a name contradicting the prefix is not ours. */
    BlFeatures wrong_name;
    make_device(&wrong_name, "TIER-104233", 0x1001, macs[0]);
    CHECK(
        bl_group_score(&p, &wrong_name) < 0,
        "a contradicting device name must hard-fail\n");

    /* Case should not decide whether a worker finds a device. */
    BlFeatures lower;
    make_device(&lower, "voi-104233", 0x1001, macs[0]);
    CHECK(
        bl_group_score(&p, &lower) >= p.threshold, "name matching must be case insensitive\n");
}

static void test_single_capture(void) {
    printf("single capture\n");

    BlCaptureSet set;
    set.count = 0;
    const uint8_t mac[6] = {0x30, 0xAE, 0xA4, 0x11, 0x22, 0x33};
    make_device(&set.items[set.count++], "VOI-104233", 0x1001, mac);

    BlGroup p;
    const char* error = NULL;
    CHECK(bl_group_build(&set, &p, &error), "single capture build failed: %s\n", error ? error : "?");

    /* The id digits must be trimmed, otherwise the group only ever matches
     * the one device that was captured. */
    CHECK(strcmp(p.name_prefix, "VOI-") == 0, "prefix '%s' not trimmed\n", p.name_prefix);

    const uint8_t mac2[6] = {0x30, 0xAE, 0xA4, 0x99, 0x88, 0x77};
    BlFeatures other;
    make_device(&other, "VOI-555444", 0x4444, mac2);
    CHECK(bl_group_score(&p, &other) >= p.threshold, "sibling rejected after single capture\n");
}

static void test_generic_rejected(void) {
    printf("generic capture guard\n");

    /* Two nameless beacons with nothing in common but their shape. */
    BlCaptureSet set;
    set.count = 0;

    Adv adv;
    for(int i = 0; i < 2; i++) {
        adv_reset(&adv);
        const uint8_t flags = 0x06;
        adv_add(&adv, BL_AD_FLAGS, &flags, 1);
        const uint8_t mac[2][6] = {
            {0x11, 0x22, 0x33, 0x44, 0x55, 0x66},
            {0x99, 0x88, 0x77, 0x66, 0x55, 0x44},
        };
        bl_features_reset(&set.items[set.count]);
        feed(&set.items[set.count], &adv, FuriHalBtAdvEventTypeNonConnectableUndirected, mac[i]);
        set.count++;
    }

    BlGroup p;
    const char* error = NULL;
    CHECK(!bl_group_build(&set, &p, &error), "a featureless capture set must be refused\n");
    CHECK(error != NULL, "refusal must explain itself\n");
    if(error) printf("  (refused with: %s)\n", error);
}

static void test_persistence(void) {
    printf("persistence\n");

    BlGroupList list;
    list.count = 0;

    BlCaptureSet set;
    set.count = 0;
    const uint8_t macs[2][6] = {
        {0x30, 0xAE, 0xA4, 0x11, 0x22, 0x33},
        {0x30, 0xAE, 0xA4, 0x44, 0x55, 0x66},
    };
    make_device(&set.items[set.count++], "VOI-104233", 0x1001, macs[0]);
    make_device(&set.items[set.count++], "VOI-118874", 0x1002, macs[1]);

    BlGroup learned;
    CHECK(bl_group_build(&set, &learned, NULL), "build\n");
    snprintf(learned.name, sizeof(learned.name), "Voi Helsinki");
    CHECK(bl_group_list_add(&list, &learned), "add\n");

    const char* path = "/tmp/bl_groups_test.txt";
    CHECK(bl_group_list_save(&list, path), "save\n");

    BlGroupList loaded;
    CHECK(bl_group_list_load(&loaded, path), "load\n");
    CHECK(loaded.count == list.count, "count %u != %u\n", loaded.count, list.count);

    /* find the learned one and compare the parts that matter for matching */
    const BlGroup* back = NULL;
    for(uint8_t i = 0; i < loaded.count; i++) {
        if(strcmp(loaded.items[i].name, "Voi Helsinki") == 0) back = &loaded.items[i];
    }
    CHECK(back != NULL, "learned group not found after reload\n");

    if(back) {
        CHECK(back->crit == learned.crit, "crit 0x%08lX != 0x%08lX\n",
              (unsigned long)back->crit, (unsigned long)learned.crit);
        CHECK(back->company_id == learned.company_id, "company\n");
        CHECK(back->mfg_head_len == learned.mfg_head_len, "mfghead len\n");
        CHECK(memcmp(back->mfg_head, learned.mfg_head, learned.mfg_head_len) == 0, "mfghead\n");
        CHECK(strcmp(back->name_prefix, learned.name_prefix) == 0, "nameprefix '%s'\n", back->name_prefix);
        CHECK(memcmp(back->oui, learned.oui, 3) == 0, "oui\n");
        CHECK(back->uuid16_count == learned.uuid16_count, "uuid16 count\n");
        CHECK(back->threshold == learned.threshold, "threshold\n");
        CHECK(back->adtype_mask == learned.adtype_mask, "adtypes\n");

        /* and it must still match the group after a round trip */
        const uint8_t mac3[6] = {0x30, 0xAE, 0xA4, 0xAA, 0xBB, 0xCC};
        BlFeatures probe;
        make_device(&probe, "VOI-321321", 0x5005, mac3);
        CHECK(
            bl_group_score(back, &probe) >= back->threshold,
            "reloaded group no longer matches its own members\n");
    }

}

static void test_hex_helpers(void) {
    printf("hex helpers\n");

    char buf[8];
    const uint8_t data[4] = {0xDE, 0xAD, 0xBE, 0xEF};

    bl_hex_to_string(data, 4, buf, sizeof(buf), false);
    CHECK(strcmp(buf, "DEADBE") == 0, "truncated hex is '%s'\n", buf);

    char big[32];
    bl_hex_to_string(data, 4, big, sizeof(big), true);
    CHECK(strcmp(big, "DE AD BE EF") == 0, "spaced hex is '%s'\n", big);

    uint8_t out[4];
    CHECK(bl_hex_from_string("DE AD BE EF", out, 4) == 4, "hex parse count\n");
    CHECK(memcmp(out, data, 4) == 0, "hex parse value\n");

    uint8_t oui[3];
    CHECK(bl_oui_from_string("30:AE:A4", oui), "oui parse\n");
    CHECK(oui[0] == 0x30 && oui[1] == 0xAE && oui[2] == 0xA4, "oui value\n");
    CHECK(!bl_oui_from_string("30:AE", oui), "short oui must fail\n");
}

/* The AD parser eats bytes straight off the air, so it has to survive anything
 * a hostile or simply broken advertiser puts on the channel. */
static void test_fuzz_parser(void) {
    printf("parser fuzz\n");

    unsigned int seed = 12345;
    uint8_t payload[64];

    for(int round = 0; round < 200000; round++) {
        seed = seed * 1103515245u + 12345u;
        const uint8_t len = (uint8_t)((seed >> 16) % 32);

        for(uint8_t i = 0; i < len; i++) {
            seed = seed * 1103515245u + 12345u;
            payload[i] = (uint8_t)(seed >> 16);
        }

        BlFeatures f;
        bl_features_reset(&f);

        FuriHalBtAdvReport r;
        r.event_type = (uint8_t)(seed % 5);
        r.address_type = (uint8_t)(seed % 4);
        memset(r.address, (int)(seed & 0xFF), 6);
        r.rssi = -60;
        r.data_len = len;
        r.data = payload;

        bl_features_apply(&f, &r);

        /* Whatever came in, the parsed view must stay inside its own bounds */
        if(f.name_len > BL_NAME_MAX) {
            CHECK(0, "name_len %u out of range\n", f.name_len);
            break;
        }
        if(f.uuid16_count > BL_UUID16_MAX) {
            CHECK(0, "uuid16_count %u out of range\n", f.uuid16_count);
            break;
        }
        if(f.mfg_head_len > BL_MFG_HEAD_MAX) {
            CHECK(0, "mfg_head_len %u out of range\n", f.mfg_head_len);
            break;
        }
        if(f.raw_adv_len > BL_RAW_MAX || f.raw_rsp_len > BL_RAW_MAX) {
            CHECK(0, "raw len out of range\n");
            break;
        }
    }

    checks++; /* survived */
}



/* --- members: the devices a group can be rebuilt from --------------------- */

static void test_members(void) {
    printf("group members\n");

    BlCaptureSet set;
    set.count = 0;
    const uint8_t macs[3][6] = {
        {0x30, 0xAE, 0xA4, 0x11, 0x22, 0x33},
        {0x30, 0xAE, 0xA4, 0x44, 0x55, 0x66},
        {0x30, 0xAE, 0xA4, 0x77, 0x88, 0x99},
    };
    make_device(&set.items[set.count++], "VOI-104233", 0x1001, macs[0]);
    make_device(&set.items[set.count++], "VOI-118874", 0x1002, macs[1]);
    make_device(&set.items[set.count++], "VOI-200019", 0x1003, macs[2]);

    /* one of them also answered a scan request */
    Adv rsp;
    adv_reset(&rsp);
    adv_add_str(&rsp, BL_AD_NAME_COMPLETE, "VOI-200019");
    feed(&set.items[2], &rsp, FuriHalBtAdvEventTypeScanResponse, macs[2]);
    CHECK(set.items[2].raw_rsp_len > 0, "scan response not recorded\n");

    BlGroup before;
    CHECK(bl_group_build(&set, &before, NULL), "build from members\n");

    const char* path = "/tmp/bl_members_test.txt";
    CHECK(bl_members_save(&set, path), "save members\n");

    BlCaptureSet back;
    CHECK(bl_members_load(&back, path), "load members\n");
    CHECK(back.count == set.count, "member count %u != %u\n", back.count, set.count);

    for(uint8_t i = 0; i < back.count && i < set.count; i++) {
        CHECK(memcmp(back.items[i].mac, set.items[i].mac, 6) == 0, "member %u mac\n", i);
        CHECK(back.items[i].addr_type == set.items[i].addr_type, "member %u addrtype\n", i);
        CHECK(
            strcmp(back.items[i].name, set.items[i].name) == 0,
            "member %u name '%s' != '%s'\n",
            i,
            back.items[i].name,
            set.items[i].name);
        CHECK(
            back.items[i].company_id == set.items[i].company_id, "member %u company\n", i);
        CHECK(
            back.items[i].connectable == set.items[i].connectable,
            "member %u connectable\n",
            i);
        CHECK(
            back.items[i].raw_adv_len == set.items[i].raw_adv_len &&
                memcmp(
                    back.items[i].raw_adv, set.items[i].raw_adv, set.items[i].raw_adv_len) == 0,
            "member %u raw advert\n",
            i);
        CHECK(
            back.items[i].raw_rsp_len == set.items[i].raw_rsp_len &&
                memcmp(
                    back.items[i].raw_rsp, set.items[i].raw_rsp, set.items[i].raw_rsp_len) == 0,
            "member %u scan response\n",
            i);
    }

    /* the point of storing them: the same group comes back out */
    BlGroup after;
    CHECK(bl_group_build(&back, &after, NULL), "rebuild from reloaded members\n");
    CHECK(after.crit == before.crit, "crit 0x%08lX != 0x%08lX\n",
          (unsigned long)after.crit, (unsigned long)before.crit);
    CHECK(after.company_id == before.company_id, "company\n");
    CHECK(after.mfg_head_len == before.mfg_head_len, "mfghead len\n");
    CHECK(memcmp(after.mfg_head, before.mfg_head, before.mfg_head_len) == 0, "mfghead\n");
    CHECK(strcmp(after.name_prefix, before.name_prefix) == 0, "nameprefix\n");
    CHECK(memcmp(after.oui, before.oui, 3) == 0, "oui\n");
    CHECK(after.adtype_mask == before.adtype_mask, "adtypes\n");

    /* adding a member later must widen the fingerprint, not break it */
    const uint8_t mac4[6] = {0x30, 0xAE, 0xA4, 0xAA, 0xBB, 0xCC};
    make_device(&back.items[back.count++], "VOI-555444", 0x1004, mac4);
    BlGroup grown;
    CHECK(bl_group_build(&back, &grown, NULL), "rebuild after adding a member\n");
    for(uint8_t i = 0; i < back.count; i++) {
        CHECK(
            bl_group_score(&grown, &back.items[i]) >= grown.threshold,
            "member %u excluded by the group it belongs to\n",
            i);
    }

    /* a missing file is an empty set, not a failure to be reported as data */
    BlCaptureSet none;
    none.count = 99;
    CHECK(!bl_members_load(&none, "/tmp/bl_members_absent.txt"), "absent file must report\n");
    CHECK(none.count == 0, "absent file left count at %u\n", none.count);

    bl_members_remove(path);
    CHECK(!bl_members_load(&none, path), "removed file must be gone\n");
}

static void test_group_ids(void) {
    printf("group ids\n");

    BlGroupList list;
    memset(&list, 0, sizeof(list));
    list.count = 3;
    snprintf(list.items[0].name, sizeof(list.items[0].name), "a");
    snprintf(list.items[1].name, sizeof(list.items[1].name), "b");
    snprintf(list.items[2].name, sizeof(list.items[2].name), "c");
    list.items[1].id = 7;
    list.items[2].id = 7; /* a hand-edited file could do this */

    bl_group_list_assign_ids(&list);

    CHECK(list.items[0].id != 0, "unassigned group kept id 0\n");
    CHECK(
        list.items[0].id != list.items[1].id && list.items[1].id != list.items[2].id &&
            list.items[0].id != list.items[2].id,
        "ids %u/%u/%u are not unique\n",
        list.items[0].id,
        list.items[1].id,
        list.items[2].id);
    CHECK(list.items[1].id == 7, "an id that was already unique must be left alone\n");

    const uint16_t next = bl_group_list_next_id(&list);
    for(uint8_t i = 0; i < list.count; i++) {
        CHECK(next != list.items[i].id, "next id %u collides with group %u\n", next, i);
    }
}

/* --- frozen row order ---------------------------------------------------- */

static void row(BlDeviceView* v, uint8_t tag, int8_t rssi) {
    memset(v, 0, sizeof(*v));
    v->mac[0] = 0xAA;
    v->mac[5] = tag;
    v->rssi = rssi;
    snprintf(v->label, sizeof(v->label), "dev%u", tag);
}

/* rows sorted strongest-first, as bl_scanner_snapshot() hands them over */
static size_t rows_from(BlDeviceView* out, const uint8_t* tags, const int8_t* rssi, size_t n) {
    for(size_t i = 0; i < n; i++) row(&out[i], tags[i], rssi[i]);
    /* insertion sort, strongest first */
    for(size_t i = 1; i < n; i++) {
        for(size_t j = i; j > 0 && out[j].rssi > out[j - 1].rssi; j--) {
            const BlDeviceView t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    }
    return n;
}

static void check_tags(const BlDeviceView* rows, size_t n, const char* expect, const char* what) {
    char got[64];
    size_t pos = 0;
    for(size_t i = 0; i < n && pos + 2 < sizeof(got); i++) {
        got[pos++] = (char)('0' + rows[i].mac[5]);
    }
    got[pos] = '\0';
    CHECK(strcmp(got, expect) == 0, "%s: got '%s', expected '%s'\n", what, got, expect);
}

static void test_row_order(void) {
    printf("frozen row order\n");

    BlDeviceView rows[BL_SNAPSHOT_MAX];
    BlRowOrder order;
    bl_order_reset(&order);

    /* settle window closes on three devices, strongest first */
    const uint8_t tags[3] = {1, 2, 3};
    const int8_t start_rssi[3] = {-50, -60, -70};
    size_t n = rows_from(rows, tags, start_rssi, 3);
    bl_order_freeze(&order, rows, n);
    check_tags(rows, n, "123", "freeze takes the signal order");

    /* signals invert completely - positions must not move */
    const int8_t flipped[3] = {-80, -60, -40};
    n = rows_from(rows, tags, flipped, 3);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "123", "order must survive an inverted signal ranking");
    CHECK(rows[0].rssi == -80, "row 1 must carry its live signal, got %d\n", rows[0].rssi);
    CHECK(rows[2].rssi == -40, "row 3 must carry its live signal, got %d\n", rows[2].rssi);

    /* device 2 goes quiet: 1 and 3 keep their relative order */
    const uint8_t gone[2] = {1, 3};
    const int8_t gone_rssi[2] = {-80, -40};
    n = rows_from(rows, gone, gone_rssi, 2);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "13", "a device dropping out must not reshuffle the rest");

    /* a newcomer stronger than everything goes to the top */
    const uint8_t plus[3] = {1, 3, 9};
    const int8_t plus_rssi[3] = {-80, -40, -30};
    n = rows_from(rows, plus, plus_rssi, 3);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "913", "a strong newcomer belongs at the top");

    /* a newcomer between the two existing rows lands between them */
    const uint8_t mid[4] = {1, 3, 9, 5};
    const int8_t mid_rssi[4] = {-80, -40, -30, -60};
    n = rows_from(rows, mid, mid_rssi, 4);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "9513", "a newcomer goes above the first row weaker than it");

    /* a newcomer weaker than everything goes to the bottom */
    const uint8_t last[5] = {1, 3, 9, 5, 7};
    const int8_t last_rssi[5] = {-80, -40, -30, -60, -95};
    n = rows_from(rows, last, last_rssi, 5);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "95137", "a newcomer weaker than every row belongs at the bottom");

    /* and once placed, it stays there even as signals move around */
    const int8_t moved[5] = {-40, -90, -85, -45, -50};
    n = rows_from(rows, last, moved, 5);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "95137", "placed rows must stay put");

    /* a device that left and comes back is a newcomer again */
    bl_order_reset(&order);
    n = rows_from(rows, tags, start_rssi, 3);
    bl_order_freeze(&order, rows, n);
    const uint8_t without2[2] = {1, 3};
    const int8_t without2_rssi[2] = {-50, -70};
    n = rows_from(rows, without2, without2_rssi, 2);
    n = bl_order_apply(&order, rows, n);
    const uint8_t back[3] = {1, 3, 2};
    const int8_t back_rssi[3] = {-50, -70, -30};
    n = rows_from(rows, back, back_rssi, 3);
    n = bl_order_apply(&order, rows, n);
    check_tags(rows, n, "213", "a returning device re-enters by its signal");
}

/* Whatever the churn, every row must come out exactly once. */
static void test_row_order_integrity(void) {
    printf("row order integrity\n");

    BlDeviceView rows[BL_SNAPSHOT_MAX];
    BlRowOrder order;
    bl_order_reset(&order);

    uint32_t seed = 0x1234567u;
    for(int iter = 0; iter < 20000; iter++) {
        seed = seed * 1103515245u + 12345u;
        const size_t n = (seed >> 16) % (BL_SNAPSHOT_MAX + 3); /* deliberately overshoots */

        for(size_t i = 0; i < n && i < BL_SNAPSHOT_MAX + 2; i++) {
            seed = seed * 1103515245u + 12345u;
            row(&rows[i < BL_SNAPSHOT_MAX ? i : BL_SNAPSHOT_MAX - 1],
                (uint8_t)((seed >> 20) % 40),
                (int8_t)(-100 + (int)((seed >> 8) % 60)));
        }
        const size_t in = n > BL_SNAPSHOT_MAX ? BL_SNAPSHOT_MAX : n;

        /* remember what went in */
        uint8_t before[BL_SNAPSHOT_MAX];
        for(size_t i = 0; i < in; i++) before[i] = rows[i].mac[5];

        const size_t out = bl_order_apply(&order, rows, n);
        if(out > BL_SNAPSHOT_MAX) {
            CHECK(0, "bl_order_apply returned %zu rows\n", out);
            return;
        }

        /* same multiset in and out - nothing duplicated, nothing lost */
        int in_count[256] = {0}, out_count[256] = {0};
        for(size_t i = 0; i < in; i++) in_count[before[i]]++;
        for(size_t i = 0; i < out; i++) out_count[rows[i].mac[5]]++;
        for(int t = 0; t < 256; t++) {
            if(in_count[t] == out_count[t]) continue;
            CHECK(0, "iter %d: tag %d in=%d out=%d\n", iter, t, in_count[t], out_count[t]);
            return;
        }
        /* the label must have travelled with the row it belongs to */
        for(size_t i = 0; i < out; i++) {
            char want[16];
            snprintf(want, sizeof(want), "dev%u", rows[i].mac[5]);
            if(strcmp(rows[i].label, want) == 0) continue;
            CHECK(0, "iter %d: row %zu label '%s' != '%s'\n", iter, i, rows[i].label, want);
            return;
        }
    }
    checks++; /* survived */
}

/* A group is only useful if it separates the group from its surroundings. */
static void test_tightening(void) {
    printf("discriminative tightening\n");

    BlCaptureSet set;
    set.count = 0;
    const uint8_t macs[3][6] = {
        {0x30, 0xAE, 0xA4, 0x11, 0x22, 0x33},
        {0x30, 0xAE, 0xA4, 0x44, 0x55, 0x66},
        {0x30, 0xAE, 0xA4, 0x77, 0x88, 0x99},
    };
    make_device(&set.items[set.count++], "VOI-104233", 0x1001, macs[0]);
    make_device(&set.items[set.count++], "VOI-118874", 0x1002, macs[1]);
    make_device(&set.items[set.count++], "VOI-200019", 0x1003, macs[2]);

    BlGroup p;
    CHECK(bl_group_build(&set, &p, NULL), "build\n");

    /* Case 1: the crowd is all phones, which hard-fail on the company id. */
    const int8_t phones[] = {-1, -1, -1, -1, -1, -1};
    BlBuildReport rep;
    bl_group_tighten(&p, &set, phones, COUNT_OF(phones), &rep);
    CHECK(rep.separated, "phones should not match a device group\n");
    CHECK(rep.others_matched == 0, "others_matched %u\n", rep.others_matched);
    CHECK(rep.others_tested == COUNT_OF(phones), "others_tested %u\n", rep.others_tested);
    CHECK(rep.min_capture_score == 100, "captures should score 100, got %u\n", rep.min_capture_score);

    /* An unseen sibling must still get through after tightening. */
    const uint8_t mac4[6] = {0x30, 0xAE, 0xA4, 0xAB, 0xCD, 0xEF};
    BlFeatures sibling;
    make_device(&sibling, "VOI-999001", 0x2001, mac4);
    CHECK(
        bl_group_score(&p, &sibling) >= p.threshold,
        "tightening must not lock out unseen group members\n");

    /* Case 2: something nearby scores 62. The threshold must rise above it
     * while still admitting the captures. */
    BlGroup p2;
    CHECK(bl_group_build(&set, &p2, NULL), "build2\n");
    const int8_t crowd[] = {-1, 40, 62, 12, -1};
    bl_group_tighten(&p2, &set, crowd, COUNT_OF(crowd), &rep);
    CHECK(p2.threshold > 62, "threshold %u must clear the best impostor (62)\n", p2.threshold);
    CHECK(p2.threshold <= 100, "threshold %u out of range\n", p2.threshold);
    CHECK(rep.separated, "should separate from a 62%% impostor\n");
    CHECK(rep.max_other_score == 62, "max_other_score %d\n", rep.max_other_score);
    for(uint8_t i = 0; i < set.count; i++) {
        CHECK(
            bl_group_score(&p2, &set.items[i]) >= p2.threshold,
            "capture %u excluded by its own group\n", i);
    }

    /* Case 3: impostors score exactly like the captures - no gap exists, and
     * the app must say so rather than pretend. */
    BlGroup p3;
    CHECK(bl_group_build(&set, &p3, NULL), "build3\n");
    const int8_t identical[] = {100, 100, 100, -1};
    bl_group_tighten(&p3, &set, identical, COUNT_OF(identical), &rep);
    CHECK(!rep.separated, "must report failure to separate\n");
    CHECK(rep.others_matched == 3, "others_matched %u, expected 3\n", rep.others_matched);

    /* Case 4: nothing else on air at all - keep the default, stay usable. */
    BlGroup p4;
    CHECK(bl_group_build(&set, &p4, NULL), "build4\n");
    const uint8_t default_threshold = p4.threshold;
    bl_group_tighten(&p4, &set, NULL, 0, &rep);
    CHECK(p4.threshold == default_threshold, "threshold changed with no evidence\n");
    CHECK(rep.others_tested == 0, "others_tested %u\n", rep.others_tested);
    CHECK(rep.separated, "vacuously separated\n");
}

int main(void) {
    test_parsing();
    test_truncated_input();
    test_learn_and_match();
    test_single_capture();
    test_generic_rejected();
    test_persistence();
    test_hex_helpers();
    test_fuzz_parser();
    test_members();
    test_group_ids();
    test_row_order();
    test_row_order_integrity();
    test_tightening();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
