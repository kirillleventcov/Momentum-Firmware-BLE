/**
 * @file bl_scanner.h
 * Live BLE scan engine: aggregates advertising reports per device, merges scan
 * responses, scores every device against the loaded groups and keeps a
 * rolling table the UI can snapshot.
 */

#pragma once

#include "bl_group.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BL_DEV_MAX      (48) /**< devices tracked simultaneously */
#define BL_SNAPSHOT_MAX (32) /**< rows the UI can pull at once */

/** Compact device row handed to the UI */
typedef struct {
    uint8_t mac[6];
    char label[BL_NAME_MAX + 1];
    int8_t rssi;
    int8_t rssi_peak;
    int8_t score; /**< -1 when nothing matched */
    int8_t group_idx; /**< -1 when nothing matched */
    uint32_t age_ms;
    uint32_t packets;
    uint16_t pps_x10; /**< packets per second, x10 */
    bool captured; /**< already added to the current learn set */
    bool matched;
} BlDeviceView;

typedef struct {
    uint32_t reports; /**< advertising reports received */
    uint32_t devices; /**< distinct devices in the table */
    uint32_t matches; /**< devices currently matching a group */
    uint32_t dropped; /**< reports lost because the queue was full */
    uint32_t started_at;
} BlScanStats;

typedef struct BlScanner BlScanner;

BlScanner* bl_scanner_alloc(void);
void bl_scanner_free(BlScanner* scanner);

/** Replace the group set used for live matching. Copies the list. */
void bl_scanner_set_groups(BlScanner* scanner, const BlGroupList* list);

/** Start the radio and the worker thread.
 *
 * @param active_scan  request scan responses (finds names hidden in SCAN_RSP)
 * @param low_power    longer interval / shorter window
 */
bool bl_scanner_start(BlScanner* scanner, bool active_scan, bool low_power);
void bl_scanner_stop(BlScanner* scanner);
bool bl_scanner_is_running(const BlScanner* scanner);

/** Forget every tracked device (keeps the radio running) */
void bl_scanner_clear(BlScanner* scanner);

/** Copy the strongest devices into `out`, sorted by smoothed RSSI.
 *
 * @param only_matched  skip devices that match no group
 * @param matched_first put group hits above stronger non-hits
 * @param rssi_min      ignore anything weaker than this
 * @param max_age_ms    ignore anything not heard from recently
 * @return              number of rows written
 */
size_t bl_scanner_snapshot(
    BlScanner* scanner,
    BlDeviceView* out,
    size_t max,
    bool only_matched,
    bool matched_first,
    int8_t rssi_min,
    uint32_t max_age_ms);

/** Look up a single device by MAC. @return false if it has aged out */
bool bl_scanner_get_view(BlScanner* scanner, const uint8_t mac[6], BlDeviceView* out);

/** Score `group` against every tracked device that is not in `exclude`.
 *
 * Used when building a fingerprint: the devices you did *not* capture are the
 * population the group has to be distinguishable from.
 *
 * @param scores_out  receives one bl_group_score() result per device tested
 * @return            number of devices scored
 */
size_t bl_scanner_score_others(
    BlScanner* scanner,
    const BlGroup* group,
    const BlCaptureSet* exclude,
    int8_t* scores_out,
    size_t max_scores);

/** Copy the full feature set of one device (for capture / raw display) */
bool bl_scanner_get_features(BlScanner* scanner, const uint8_t mac[6], BlFeatures* out);

/** Mark/unmark a device as captured so the list can show it */
void bl_scanner_set_captured(BlScanner* scanner, const uint8_t mac[6], bool captured);

void bl_scanner_get_stats(BlScanner* scanner, BlScanStats* out);

/** Start/stop appending every report to a CSV file. */
bool bl_scanner_log_start(BlScanner* scanner, const char* path);
void bl_scanner_log_stop(BlScanner* scanner);
bool bl_scanner_log_is_active(const BlScanner* scanner);

#ifdef __cplusplus
}
#endif
