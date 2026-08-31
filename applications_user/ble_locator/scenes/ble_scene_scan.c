#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <string.h>

static const NotificationSequence bl_seq_capture = {
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_note_c5,
    &message_delay_50,
    &message_sound_off,
    &message_green_255,
    &message_delay_100,
    &message_green_0,
    NULL,
};

static const NotificationSequence bl_seq_reject = {
    &message_note_c4,
    &message_delay_100,
    &message_sound_off,
    &message_red_255,
    &message_delay_100,
    &message_red_0,
    NULL,
};

static void ble_scene_scan_callback(uint32_t event, void* context) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

/* Begin a settle window: forget what was on air a moment ago and collect from
 * scratch for the configured number of seconds. */
static void ble_scene_scan_settle_start(BlApp* app) {
    bl_scanner_clear(app->scanner);
    bl_order_reset(&app->order);
    app->settling = true;
    app->settle_started = furi_get_tick();
    bl_view_list_reset(app->view_list);
    bl_view_list_set_settling(
        app->view_list, true, app->settings.settle_s, app->settings.settle_s);
}

void ble_scene_scan_on_enter(void* context) {
    BlApp* app = context;

    bl_view_list_set_callback(app->view_list, ble_scene_scan_callback, app);
    bl_view_list_set_mode(app->view_list, app->scan_mode);

    bl_app_apply_groups(app);

    if(!bl_app_scan_start(app)) {
        bl_app_show_message(
            app, BlViewIdList, "Scan failed", "BLE radio not ready.\nToggle Bluetooth off/on.");
        return;
    }

    /* Only a menu asks for a settle. Coming back from the homing or details
     * screen keeps the order you were already looking at. */
    if(app->settle_pending) {
        app->settle_pending = false;
        ble_scene_scan_settle_start(app);
    } else {
        bl_view_list_set_settling(app->view_list, false, 0, app->settings.settle_s);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdList);
}

static void ble_scene_scan_refresh(BlApp* app) {
    BlDeviceView* rows = app->snapshot;

    /* "Scan all" is the analysis mode: nothing is hidden. "Find a group" is the
     * walk-past mode: the screen stays empty until a group member is in range. */
    const bool only_matched = (app->scan_mode == BlScanModeGroup);
    /* Always pure signal order: the frozen list is meant to read as a map of
     * what is near you, so floating group hits above closer unknowns would
     * make the top of the list mean two different things at once. */
    const bool matched_first = false;

    size_t count = bl_scanner_snapshot(
        app->scanner,
        rows,
        BL_SNAPSHOT_MAX,
        only_matched,
        matched_first,
        app->settings.rssi_min,
        /* keep a device listed for 30 s after its last packet */ 30000);

    /* The capture set doubles as a group's member list, so only mark rows in
     * the modes where it means "already picked" rather than "already a member
     * of some group you happened to open the editor for". */
    const bool mark_captured =
        (app->scan_mode == BlScanModeLearn) || (app->scan_mode == BlScanModeAddMember);
    for(size_t i = 0; i < count; i++) {
        rows[i].captured = mark_captured && bl_app_capture_contains(app, rows[i].mac);
    }

    if(app->settling) {
        const uint32_t total_ms = (uint32_t)app->settings.settle_s * 1000;
        const uint32_t elapsed = furi_get_tick() - app->settle_started;

        if(elapsed < total_ms) {
            /* Rows are still moving; show the countdown, not the list. */
            const uint32_t left_ms = total_ms - elapsed;
            bl_view_list_set_settling(
                app->view_list,
                true,
                (uint8_t)((left_ms + 999) / 1000),
                app->settings.settle_s);
        } else {
            /* The snapshot arrives sorted by signal, so freezing it here is
             * exactly "closest at the top, furthest at the bottom". */
            bl_order_freeze(&app->order, rows, count);
            app->settling = false;
            bl_view_list_set_settling(app->view_list, false, 0, app->settings.settle_s);
        }
    }

    if(!app->settling) {
        count = bl_order_apply(&app->order, rows, count);
        bl_view_list_set_rows(app->view_list, rows, count);
    }

    BlScanStats stats;
    bl_scanner_get_stats(app->scanner, &stats);
    bl_view_list_set_status(
        app->view_list,
        stats.devices,
        stats.matches,
        app->captures.count,
        bl_scanner_is_running(app->scanner),
        bl_scanner_log_is_active(app->scanner));
    bl_view_list_set_group(
        app->view_list,
        (app->scan_mode == BlScanModeAddMember) ? app->groups.items[app->selected_group].name :
                                                  bl_app_group_lock(app));
    bl_view_list_set_mode(app->view_list, app->scan_mode);
}

static void ble_scene_scan_capture_mac(BlApp* app, const uint8_t mac[6]) {
    switch(bl_app_capture(app, mac)) {
    case BlCaptureOk:
        notification_message(app->notifications, &bl_seq_capture);
        break;
    case BlCaptureDuplicate:
        bl_app_show_message(app, BlViewIdList, "Already captured", "Pick another device");
        notification_message(app->notifications, &bl_seq_reject);
        break;
    case BlCaptureFull:
        bl_app_show_message(app, BlViewIdList, "Capture set full", "Press Right to build");
        notification_message(app->notifications, &bl_seq_reject);
        break;
    case BlCaptureGone:
    default:
        bl_app_show_message(app, BlViewIdList, "Out of range", "Move closer and retry");
        notification_message(app->notifications, &bl_seq_reject);
        break;
    }
}

/* Adding to an existing group: take the one device, rebuild the fingerprint
 * from the members plus it, and let the review screen have the last word. The
 * stored group is not touched until Save. */
static void ble_scene_scan_add_member(BlApp* app, const uint8_t mac[6]) {
    const uint8_t before = app->captures.count;

    switch(bl_app_capture(app, mac)) {
    case BlCaptureOk:
        break;
    case BlCaptureDuplicate:
        bl_app_show_message(app, BlViewIdList, "Already a member", "Pick another device");
        notification_message(app->notifications, &bl_seq_reject);
        return;
    case BlCaptureFull:
        bl_app_show_message(app, BlViewIdList, "Group is full", "Remove a member first");
        notification_message(app->notifications, &bl_seq_reject);
        return;
    case BlCaptureGone:
    default:
        bl_app_show_message(app, BlViewIdList, "Out of range", "Move closer and retry");
        notification_message(app->notifications, &bl_seq_reject);
        return;
    }

    const char* error = NULL;
    if(!bl_app_rebuild_group(app, &error)) {
        app->captures.count = before;
        bl_app_show_message(app, BlViewIdList, "Does not fit", error ? error : "Unknown problem");
        notification_message(app->notifications, &bl_seq_reject);
        return;
    }

    notification_message(app->notifications, &bl_seq_capture);
    scene_manager_next_scene(app->scene_manager, BlSceneLearnReview);
}

static void ble_scene_scan_capture(BlApp* app) {
    uint8_t mac[6];
    if(!bl_view_list_get_selected(app->view_list, mac)) return;
    if(app->scan_mode == BlScanModeAddMember) {
        ble_scene_scan_add_member(app, mac);
    } else {
        ble_scene_scan_capture_mac(app, mac);
    }
}

static void ble_scene_scan_undo(BlApp* app) {
    if(bl_app_capture_undo(app)) {
        notification_message(app->notifications, &bl_seq_reject);
    }
}

static void ble_scene_scan_build(BlApp* app) {
    if(app->captures.count == 0) {
        bl_app_show_message(app, BlViewIdList, "Nothing captured", "Press OK on a device");
        return;
    }

    const char* error = NULL;
    if(!bl_app_build_group(app, &error)) {
        bl_app_show_message(app, BlViewIdList, "Cannot build", error ? error : "Unknown problem");
        notification_message(app->notifications, &bl_seq_reject);
        return;
    }

    app->pending_kind = BlPendingLearn;
    scene_manager_next_scene(app->scene_manager, BlSceneLearnReview);
}

bool ble_scene_scan_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type == SceneManagerEventTypeTick) {
        ble_scene_scan_refresh(app);
        return true;
    }

    if(event.type != SceneManagerEventTypeCustom) return false;

    switch(event.event) {
    case BlCustomEventSelect: {
        uint8_t mac[6];
        if(bl_view_list_get_selected(app->view_list, mac)) {
            memcpy(app->target_mac, mac, 6);
            scene_manager_next_scene(app->scene_manager, BlSceneTrack);
        }
        return true;
    }

    case BlCustomEventInfo: {
        uint8_t mac[6];
        if(bl_view_list_get_selected(app->view_list, mac)) {
            memcpy(app->target_mac, mac, 6);
            scene_manager_next_scene(app->scene_manager, BlSceneDeviceInfo);
        }
        return true;
    }

    case BlCustomEventCapture:
        ble_scene_scan_capture(app);
        return true;

    case BlCustomEventCaptureTarget:
        if(app->scan_mode == BlScanModeAddMember) {
            ble_scene_scan_add_member(app, app->target_mac);
        } else {
            ble_scene_scan_capture_mac(app, app->target_mac);
        }
        return true;

    case BlCustomEventUndo:
        ble_scene_scan_undo(app);
        return true;

    case BlCustomEventResettle:
        ble_scene_scan_settle_start(app);
        return true;

    case BlCustomEventBuild:
        ble_scene_scan_build(app);
        return true;

    default:
        return false;
    }
}

void ble_scene_scan_on_exit(void* context) {
    BlApp* app = context;
    popup_reset(app->popup);
    bl_view_list_set_callback(app->view_list, NULL, NULL);
}
