/**
 * @file bl_adv.h
 * BLE advertisement parsing: turns raw AD structures into a feature set that
 * can be fingerprinted and matched.
 */

#pragma once

#include <furi.h>
#include <furi_hal_bt.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BL_NAME_MAX     (26)
#define BL_UUID16_MAX   (4)
#define BL_MFG_HEAD_MAX (8)
#define BL_RAW_MAX      (31)

/* GAP AD types we care about */
#define BL_AD_FLAGS           0x01
#define BL_AD_UUID16_MORE     0x02
#define BL_AD_UUID16_ALL      0x03
#define BL_AD_UUID32_MORE     0x04
#define BL_AD_UUID32_ALL      0x05
#define BL_AD_UUID128_MORE    0x06
#define BL_AD_UUID128_ALL     0x07
#define BL_AD_NAME_SHORT      0x08
#define BL_AD_NAME_COMPLETE   0x09
#define BL_AD_TX_POWER        0x0A
#define BL_AD_SVC_DATA_UUID16 0x16
#define BL_AD_APPEARANCE      0x19
#define BL_AD_SVC_DATA_UUID32 0x20
#define BL_AD_SVC_DATA_UUID12 0x21
#define BL_AD_MANUFACTURER    0xFF

/** Everything we know about one advertiser, merged across ADV_IND and SCAN_RSP. */
typedef struct {
    uint8_t mac[6]; /**< display order, most significant byte first */
    uint8_t addr_type;

    char name[BL_NAME_MAX + 1];
    uint8_t name_len;

    uint16_t uuid16[BL_UUID16_MAX];
    uint8_t uuid16_count;

    uint8_t uuid128[16];
    bool has_uuid128;

    uint16_t company_id;
    bool has_company;
    uint8_t mfg_head[BL_MFG_HEAD_MAX]; /**< first bytes after the company id */
    uint8_t mfg_head_len;
    uint8_t mfg_len; /**< full manufacturer data length, company id included */

    uint32_t adtype_mask; /**< bit N = AD type N seen (types >= 31 fold into bit 31) */

    uint8_t flags;
    bool has_flags;
    int8_t tx_power;
    bool has_tx_power;
    uint16_t appearance;
    bool has_appearance;
    uint16_t svc_data_uuid16;
    bool has_svc_data;

    bool connectable;
    bool had_scan_rsp;

    uint8_t raw_adv[BL_RAW_MAX];
    uint8_t raw_adv_len;
    uint8_t raw_rsp[BL_RAW_MAX];
    uint8_t raw_rsp_len;
} BlFeatures;

/** Reset a feature set to "nothing known" */
void bl_features_reset(BlFeatures* f);

/** Merge one advertising report into a feature set.
 *
 * Safe to call repeatedly with ADV_IND and SCAN_RSP reports of the same device:
 * fields are only ever added, never cleared.
 *
 * @return true if the feature set changed in a way that affects matching
 */
bool bl_features_apply(BlFeatures* f, const FuriHalBtAdvReport* report);

/** Format a MAC as AA:BB:CC:DD:EE:FF */
void bl_mac_to_string(const uint8_t mac[6], char* out, size_t out_size);

/** Parse AA:BB:CC (or AABBCC) into 3 OUI bytes. @return true on success */
bool bl_oui_from_string(const char* str, uint8_t out[3]);

/** Hex-dump `len` bytes into `out` as "AA BB CC" */
void bl_hex_to_string(const uint8_t* data, size_t len, char* out, size_t out_size, bool spaced);

/** Parse a hex string ("AABB" or "AA BB") into bytes. @return bytes written */
size_t bl_hex_from_string(const char* str, uint8_t* out, size_t out_size);

/** Multi-line dump of everything parsed out of one advertiser. */
void bl_features_describe(const BlFeatures* f, FuriString* out);

/** Human label for an advertiser: its name, or "MAC (no name)" */
void bl_features_label(const BlFeatures* f, char* out, size_t out_size);

#ifdef __cplusplus
}
#endif
