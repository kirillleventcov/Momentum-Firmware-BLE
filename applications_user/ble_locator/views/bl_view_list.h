#pragma once

#include "../helpers/bl_scanner.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the list is showing. Also decides what OK does on a row.
 *
 * Lives here rather than in the app header because the list view is the thing
 * that renders each mode, and the app header includes this one. */
typedef enum {
    BlScanModeAll, /**< every advertiser on air, known ones labelled */
    BlScanModeGroup, /**< only devices matching the selected group(s) */
    BlScanModeLearn, /**< capture advertisers to build a group from */
    BlScanModeAddMember, /**< pick one advertiser to add to an existing group */
    BlScanModeRetest, /**< settle, then re-tighten a group against what is on air */
} BlScanMode;

typedef struct BlViewList BlViewList;

typedef void (*BlViewListCallback)(uint32_t event, void* context);

BlViewList* bl_view_list_alloc(void);
void bl_view_list_free(BlViewList* view_list);
View* bl_view_list_get_view(BlViewList* view_list);

void bl_view_list_set_callback(BlViewList* view_list, BlViewListCallback callback, void* context);

/** Learn mode changes the hints and lets OK capture instead of track */
void bl_view_list_set_mode(BlViewList* view_list, BlScanMode mode);

void bl_view_list_set_status(
    BlViewList* view_list,
    uint32_t devices,
    uint32_t matches,
    uint8_t captured,
    bool scanning,
    bool logging);

/** Show the settle screen instead of the list, and swallow input while it is up.
 *
 * @param remaining_s  seconds left, rounded up
 * @param total_s      the configured settle length, for the progress bar
 */
void bl_view_list_set_settling(
    BlViewList* view_list,
    bool settling,
    uint8_t remaining_s,
    uint8_t total_s);

/** Name of the group the scan is locked to, or NULL for "any known group" */
void bl_view_list_set_group(BlViewList* view_list, const char* group);

/** Replace the visible rows. Keeps the selection on the same MAC when possible. */
void bl_view_list_set_rows(BlViewList* view_list, const BlDeviceView* rows, size_t count);

/** MAC of the highlighted row. @return false when the list is empty */
bool bl_view_list_get_selected(BlViewList* view_list, uint8_t mac[6]);

void bl_view_list_reset(BlViewList* view_list);

#ifdef __cplusplus
}
#endif
