#pragma once

#include "helpers/bl_adv.h"
#include "helpers/bl_group.h"
#include "helpers/bl_members.h"
#include "helpers/bl_order.h"
#include "helpers/bl_scanner.h"
#include "views/bl_view_list.h"
#include "views/bl_view_track.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define BL_APP_FOLDER  EXT_PATH("apps_data/ble_locator")
#define BL_GROUP_FILE  BL_APP_FOLDER "/groups.txt"
#define BL_MEMBER_DIR  BL_APP_FOLDER "/members"
#define BL_CONFIG_FILE BL_APP_FOLDER "/settings.bin"
#define BL_LOG_FILE    BL_APP_FOLDER "/survey.csv"

/* Groups learned by the app's previous, scooter-only incarnation. Read once on
 * first run so nobody loses fingerprints they walked around to collect. */
#define BL_LEGACY_GROUP_FILE EXT_PATH("apps_data/scooter_locator/profiles.txt")

#define BL_CONFIG_VERSION (4)

/* How long a fresh scan collects before the row order is frozen. */
#define BL_SETTLE_MIN_S     (3)
#define BL_SETTLE_MAX_S     (10)
#define BL_SETTLE_DEFAULT_S (5)
#define BL_TICK_PERIOD_MS (250)

typedef enum {
    BlSensitivityStrict,
    BlSensitivityNormal,
    BlSensitivityLoose,
    BlSensitivityCount,
} BlSensitivity;

typedef struct {
    bool active_scan;
    bool low_power;
    bool sound;
    bool vibro;
    bool log_csv;
    int8_t rssi_min;
    uint8_t sensitivity;
    /* Seconds a fresh scan spends collecting before it locks the row order,
     * BL_SETTLE_MIN_S..BL_SETTLE_MAX_S. */
    uint8_t settle_s;
    /* Name of the single group "Find a group" is pointed at. Empty means every
     * enabled group. Stored by name rather than index so deleting a group
     * cannot silently repoint the lock at a different one. */
    char group_lock[BL_GROUP_NAME_MAX + 1];
} BlSettings;

typedef enum {
    BlViewIdSubmenu,
    BlViewIdVarItemList,
    BlViewIdTextInput,
    BlViewIdWidget,
    BlViewIdPopup,
    BlViewIdList,
    BlViewIdTrack,
} BlViewId;

/** Where a freshly built fingerprint is headed once the operator accepts it. */
typedef enum {
    BlPendingLearn, /**< a brand new group, still to be named */
    BlPendingEdit, /**< a rebuild of groups.items[selected_group] */
} BlPendingKind;

typedef enum {
    BlCustomEventTick = 100,
    BlCustomEventSelect,
    BlCustomEventCapture,
    BlCustomEventInfo,
    BlCustomEventBuild,
    BlCustomEventUndo,
    BlCustomEventCaptureTarget,
    BlCustomEventResettle,
} BlCustomEvent;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    NotificationApp* notifications;

    Submenu* submenu;
    VariableItemList* var_item_list;
    TextInput* text_input;
    Widget* widget;
    Popup* popup;

    BlViewList* view_list;
    BlViewTrack* view_track;

    BlScanner* scanner;
    BlGroupList groups;
    /* The sensitivity-adjusted copy handed to the scanner. Lives here because
     * BlGroupList is nearly 3 KB and the app thread has a 4 KB stack. */
    BlGroupList tuned;
    /* Doubles as the learn-session capture set and the member set of whichever
     * group is being edited - they are the same thing, a list of devices a
     * group gets intersected from. */
    BlCaptureSet captures;
    BlSettings settings;
    /* Lives here rather than on the scene stack: 32 rows is more than the
     * app thread's stack should be carrying. */
    BlDeviceView snapshot[BL_SNAPSHOT_MAX];

    FuriTimer* timer;
    BlViewId popup_return;

    BlScanMode scan_mode;
    bool bt_was_active;

    BlRowOrder order;
    bool settling;
    uint32_t settle_started;
    /* Set by the menu that opened the scan. Coming back from the homing or
     * details screen must not restart the settle and reshuffle everything. */
    bool settle_pending;
    /* Group-mode hits present at the last refresh. Anything in the current
     * snapshot but not here has just come into range and gets an alert. */
    uint8_t hit_seen[BL_SNAPSHOT_MAX][6];
    uint8_t hit_seen_count;

    uint8_t target_mac[6];
    BlGroup pending_group;
    BlBuildReport build_report;
    /* What pressing Save on the review screen should do with pending_group. */
    BlPendingKind pending_kind;
    uint8_t selected_group;
    uint8_t selected_member;
    char text_buffer[BL_GROUP_NAME_MAX + 1];
    FuriString* text_box_store;
    uint32_t last_feedback;
} BlApp;

/** Copy app groups into the scanner, applying the global sensitivity offset */
void bl_app_apply_groups(BlApp* app);

/** Persist the group list to SD */
void bl_app_save_groups(BlApp* app);

/** Persist settings to SD */
void bl_app_save_settings(BlApp* app);

/** Start/stop scanning honouring the current settings */
bool bl_app_scan_start(BlApp* app);
void bl_app_scan_stop(BlApp* app);

/** Transient popup that returns to `back_to` when it times out */
void bl_app_show_message(BlApp* app, BlViewId back_to, const char* header, const char* text);

/** Rebuild groups.items[selected_group] from app->captures into pending_group,
 * keeping its name, id and enabled flag. Leaves the stored group untouched;
 * bl_app_commit_group() is what actually replaces it.
 *
 * @return false with `error` set when the members no longer intersect usefully
 */
bool bl_app_rebuild_group(BlApp* app, const char** error);

/** Replace groups.items[selected_group] with pending_group and persist both it
 * and the member set it was built from */
void bl_app_commit_group(BlApp* app);

/** Load the members of group `index` into app->captures */
void bl_app_load_members(BlApp* app, uint8_t index);

/** Write app->captures out as the members of group `index` */
void bl_app_save_members(BlApp* app, uint8_t index);

/** Forget the member file of group `index` */
void bl_app_delete_members(BlApp* app, uint8_t index);

/** Name of a group by index, or "unknown" */
const char* bl_app_group_name(BlApp* app, int8_t index);

/** Coarse distance bucket for an RSSI value */
const char* bl_app_distance_hint(int8_t rssi);

typedef enum {
    BlCaptureOk,
    BlCaptureDuplicate,
    BlCaptureFull,
    BlCaptureGone,
} BlCaptureResult;

/** Add one advertiser to the current learn set */
BlCaptureResult bl_app_capture(BlApp* app, const uint8_t mac[6]);

/** @return true if `mac` is already in the learn set */
bool bl_app_capture_contains(BlApp* app, const uint8_t mac[6]);

/** Drop the most recent capture */
bool bl_app_capture_undo(BlApp* app);

/** Build a group from the current captures and sharpen it against everything
 * else currently on air. Fills app->pending_group and app->build_report.
 *
 * @return false with `error` set when the captures are too generic to use
 */
bool bl_app_build_group(BlApp* app, const char** error);

/** Name of the group the scan is locked to, or NULL when any group counts */
const char* bl_app_group_lock(BlApp* app);
