#include "ble_locator_i.h"
#include "scenes/ble_scene.h"

#include <toolbox/saved_struct.h>
#include <furi_hal_bt.h>

#define TAG "BleLocator"

#define BL_CONFIG_MAGIC (0x5C)

/* --- shared helpers ------------------------------------------------------ */

const char* bl_app_group_lock(BlApp* app) {
    return app->settings.group_lock[0] ? app->settings.group_lock : NULL;
}

void bl_app_apply_groups(BlApp* app) {
    BlGroupList* tuned = &app->tuned;
    *tuned = app->groups;
    const char* locked = bl_app_group_lock(app);

    /* A lock naming a group that no longer exists would silently disable every
     * group and leave the hunt blank forever. Fall back to all groups. */
    if(locked) {
        bool found = false;
        for(uint8_t i = 0; i < tuned->count; i++) {
            if(strncmp(tuned->items[i].name, locked, BL_GROUP_NAME_MAX) == 0) found = true;
        }
        if(!found) {
            app->settings.group_lock[0] = '\0';
            locked = NULL;
        }
    }

    for(uint8_t i = 0; i < tuned->count; i++) {
        /* Group lock: everything except the chosen group is switched off in the
         * copy handed to the scanner, so the scan reports that group and
         * nothing else. The stored groups keep their own enabled flags. */
        if(locked && strncmp(tuned->items[i].name, locked, BL_GROUP_NAME_MAX) != 0) {
            tuned->items[i].enabled = false;
            continue;
        }
        if(locked) tuned->items[i].enabled = true;

        int threshold = tuned->items[i].threshold;
        if(app->settings.sensitivity == BlSensitivityStrict) {
            threshold += 15;
        } else if(app->settings.sensitivity == BlSensitivityLoose) {
            threshold -= 15;
        }
        if(threshold < 10) threshold = 10;
        if(threshold > 100) threshold = 100;
        tuned->items[i].threshold = (uint8_t)threshold;
    }

    bl_scanner_set_groups(app->scanner, tuned);
}

void bl_app_save_groups(BlApp* app) {
    if(!bl_group_list_save(&app->groups, BL_GROUP_FILE)) {
        FURI_LOG_E(TAG, "Failed to save groups");
    }
}

void bl_app_save_settings(BlApp* app) {
    saved_struct_save(
        BL_CONFIG_FILE,
        &app->settings,
        sizeof(BlSettings),
        BL_CONFIG_MAGIC,
        BL_CONFIG_VERSION);
}

bool bl_app_scan_start(BlApp* app) {
    if(bl_scanner_is_running(app->scanner)) return true;

    /* Advertising steals radio time from scanning, so park it while we scan.
     * Only when it is idle advertising though - furi_hal_bt_stop_advertising()
     * tears down an active connection, and dropping someone's phone link
     * without asking is not ours to do. */
    const GapState gap_state = gap_get_state();
    app->bt_was_active =
        (gap_state == GapStateAdvFast) || (gap_state == GapStateAdvLowPower) ||
        (gap_state == GapStateStartingAdv);
    if(app->bt_was_active) {
        furi_hal_bt_stop_advertising();
    }

    if(app->settings.log_csv) {
        bl_scanner_log_start(app->scanner, BL_LOG_FILE);
    }

    const bool ok =
        bl_scanner_start(app->scanner, app->settings.active_scan, app->settings.low_power);
    if(!ok && app->bt_was_active) {
        furi_hal_bt_start_advertising();
        app->bt_was_active = false;
    }

    return ok;
}

void bl_app_scan_stop(BlApp* app) {
    bl_scanner_stop(app->scanner);
    bl_scanner_log_stop(app->scanner);

    if(app->bt_was_active) {
        furi_hal_bt_start_advertising();
        app->bt_was_active = false;
    }
}

static void bl_app_popup_done(void* context) {
    BlApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, app->popup_return);
}

void bl_app_show_message(BlApp* app, BlViewId back_to, const char* header, const char* text) {
    app->popup_return = back_to;
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 12, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 34, AlignCenter, AlignCenter);
    popup_set_timeout(app->popup, 1800);
    popup_enable_timeout(app->popup);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, bl_app_popup_done);
    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdPopup);
}

bool bl_app_rebuild_group(BlApp* app, const char** error) {
    if(app->selected_group >= app->groups.count) return false;
    if(!bl_app_build_group(app, error)) return false;

    /* A rebuild keeps the group's identity; only the fingerprint changes. */
    const BlGroup* current = &app->groups.items[app->selected_group];
    snprintf(app->pending_group.name, sizeof(app->pending_group.name), "%s", current->name);
    app->pending_group.id = current->id;
    app->pending_group.enabled = current->enabled;
    app->pending_group.builtin = false;
    app->pending_kind = BlPendingEdit;
    return true;
}

void bl_app_commit_group(BlApp* app) {
    if(app->selected_group >= app->groups.count) return;

    app->groups.items[app->selected_group] = app->pending_group;
    bl_app_save_groups(app);
    bl_app_save_members(app, app->selected_group);
    bl_app_apply_groups(app);
}

static void bl_app_member_path(BlApp* app, uint8_t index, char* out, size_t out_size) {
    bl_members_path(BL_MEMBER_DIR, app->groups.items[index].id, out, out_size);
}

void bl_app_load_members(BlApp* app, uint8_t index) {
    app->captures.count = 0;
    if(index >= app->groups.count) return;

    char path[96];
    bl_app_member_path(app, index, path, sizeof(path));
    bl_members_load(&app->captures, path);
}

void bl_app_save_members(BlApp* app, uint8_t index) {
    if(index >= app->groups.count) return;

    char path[96];
    bl_app_member_path(app, index, path, sizeof(path));
    if(!bl_members_save(&app->captures, path)) {
        FURI_LOG_E(TAG, "Failed to save members");
    }
}

void bl_app_delete_members(BlApp* app, uint8_t index) {
    if(index >= app->groups.count) return;

    char path[96];
    bl_app_member_path(app, index, path, sizeof(path));
    bl_members_remove(path);
}

const char* bl_app_group_name(BlApp* app, int8_t index) {
    if(index < 0 || index >= (int8_t)app->groups.count) return "unknown";
    return app->groups.items[index].name;
}

const char* bl_app_distance_hint(int8_t rssi) {
    /* Deliberately coarse. Turning RSSI into metres precisely is not possible
     * with an unknown TX power and a device body in the way. */
    if(rssi >= -50) return "<1 m";
    if(rssi >= -60) return "1-3 m";
    if(rssi >= -70) return "3-8 m";
    if(rssi >= -80) return "8-20 m";
    if(rssi >= -90) return "20-50 m";
    return ">50 m";
}

bool bl_app_capture_contains(BlApp* app, const uint8_t mac[6]) {
    for(uint8_t i = 0; i < app->captures.count; i++) {
        if(memcmp(app->captures.items[i].mac, mac, 6) == 0) return true;
    }
    return false;
}

BlCaptureResult bl_app_capture(BlApp* app, const uint8_t mac[6]) {
    if(bl_app_capture_contains(app, mac)) return BlCaptureDuplicate;
    if(app->captures.count >= BL_CAPTURE_MAX) return BlCaptureFull;

    BlFeatures features;
    if(!bl_scanner_get_features(app->scanner, mac, &features)) return BlCaptureGone;

    app->captures.items[app->captures.count++] = features;
    bl_scanner_set_captured(app->scanner, mac, true);
    return BlCaptureOk;
}

bool bl_app_capture_undo(BlApp* app) {
    if(app->captures.count == 0) return false;
    app->captures.count--;
    bl_scanner_set_captured(app->scanner, app->captures.items[app->captures.count].mac, false);
    return true;
}

bool bl_app_build_group(BlApp* app, const char** error) {
    if(!bl_group_build(&app->captures, &app->pending_group, error)) {
        memset(&app->build_report, 0, sizeof(app->build_report));
        return false;
    }

    /* Sharpen against the crowd: everything on air right now that we did not
     * capture is what this fingerprint has to be told apart from. */
    int8_t scores[BL_DEV_MAX];
    const size_t others = bl_scanner_score_others(
        app->scanner, &app->pending_group, &app->captures, scores, COUNT_OF(scores));

    bl_group_tighten(
        &app->pending_group, &app->captures, scores, others, &app->build_report);

    return true;
}

/* --- view dispatcher plumbing -------------------------------------------- */

static bool bl_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    BlApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool bl_back_event_callback(void* context) {
    furi_assert(context);
    BlApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void bl_tick_event_callback(void* context) {
    furi_assert(context);
    BlApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

/* --- lifecycle ------------------------------------------------------------ */

static void bl_app_load_state(BlApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, EXT_PATH("apps_data"));
    storage_common_mkdir(storage, BL_APP_FOLDER);
    storage_common_mkdir(storage, BL_MEMBER_DIR);
    furi_record_close(RECORD_STORAGE);

    /* defaults */
    app->settings.active_scan = true;
    app->settings.low_power = false;
    app->settings.sound = true;
    app->settings.vibro = true;
    app->settings.log_csv = false;
    app->settings.rssi_min = -100;
    app->settings.sensitivity = BlSensitivityNormal;
    app->settings.settle_s = BL_SETTLE_DEFAULT_S;
    app->settings.group_lock[0] = '\0';

    saved_struct_load(
        BL_CONFIG_FILE,
        &app->settings,
        sizeof(BlSettings),
        BL_CONFIG_MAGIC,
        BL_CONFIG_VERSION);

    /* A hand-edited or half-written settings file must not be able to hang the
     * UI on a settle window that never ends. */
    if(app->settings.settle_s < BL_SETTLE_MIN_S || app->settings.settle_s > BL_SETTLE_MAX_S) {
        app->settings.settle_s = BL_SETTLE_DEFAULT_S;
    }

    /* No groups ship with the app. Everything it knows, someone walked up to a
     * device and taught it, so an empty list is a legitimate state - note the
     * absent "|| count == 0" below. Treating empty as "not loaded" would
     * re-import the legacy file on every launch and undo deleting your last
     * group. */
    if(!bl_group_list_load(&app->groups, BL_GROUP_FILE)) {
        /* Same on-disk format, so an old scooter_locator profile file loads
         * straight in - minus the starter groups that version shipped, which
         * are not the operator's own work and are not carried forward. */
        if(bl_group_list_load(&app->groups, BL_LEGACY_GROUP_FILE)) {
            uint8_t kept = 0;
            for(uint8_t i = 0; i < app->groups.count; i++) {
                if(app->groups.items[i].builtin) continue;
                app->groups.items[kept++] = app->groups.items[i];
            }
            app->groups.count = kept;
            if(kept) bl_app_save_groups(app);
        }
    }

    /* Groups arriving without an id predate member editing. They get one here
     * so they have somewhere to keep members from now on; what they were
     * originally built from is gone and cannot be recovered. */
    bl_group_list_assign_ids(&app->groups);
}

static BlApp* bl_app_alloc(void) {
    BlApp* app = malloc(sizeof(BlApp));
    memset(app, 0, sizeof(BlApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->text_box_store = furi_string_alloc();

    app->scanner = bl_scanner_alloc();
    bl_app_load_state(app);
    bl_app_apply_groups(app);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&ble_scene_handlers, app);

    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, bl_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, bl_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, bl_tick_event_callback, BL_TICK_PERIOD_MS);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BlViewIdSubmenu, submenu_get_view(app->submenu));

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BlViewIdVarItemList, variable_item_list_get_view(app->var_item_list));

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BlViewIdTextInput, text_input_get_view(app->text_input));

    app->widget = widget_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BlViewIdWidget, widget_get_view(app->widget));

    app->popup = popup_alloc();
    view_dispatcher_add_view(app->view_dispatcher, BlViewIdPopup, popup_get_view(app->popup));

    app->view_list = bl_view_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BlViewIdList, bl_view_list_get_view(app->view_list));

    app->view_track = bl_view_track_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, BlViewIdTrack, bl_view_track_get_view(app->view_track));

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    return app;
}

static void bl_app_free(BlApp* app) {
    bl_app_scan_stop(app);

    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdSubmenu);
    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdWidget);
    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdPopup);
    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdList);
    view_dispatcher_remove_view(app->view_dispatcher, BlViewIdTrack);

    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    text_input_free(app->text_input);
    widget_free(app->widget);
    popup_free(app->popup);
    bl_view_list_free(app->view_list);
    bl_view_track_free(app->view_track);

    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    bl_scanner_free(app->scanner);
    furi_string_free(app->text_box_store);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t ble_locator_app(void* p) {
    UNUSED(p);

    BlApp* app = bl_app_alloc();

    if(furi_hal_bt_is_scan_supported()) {
        scene_manager_next_scene(app->scene_manager, BlSceneStart);
    } else {
        scene_manager_next_scene(app->scene_manager, BlSceneNoStack);
    }

    view_dispatcher_run(app->view_dispatcher);

    bl_app_free(app);
    return 0;
}
