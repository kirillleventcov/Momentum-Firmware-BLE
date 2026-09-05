#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <string.h>

/* Geiger-counter style homing: the closer you get, the faster it pings. */
#define BL_PING_MIN_MS (170)
#define BL_PING_MAX_MS (1600)
/* Parked modules throttle their advertising hard to save the backup
 * battery, so give a target plenty of rope before calling it lost. */
#define BL_LOST_MS     (12000)

static const NotificationSequence bl_seq_ping = {
    &message_note_c7,
    &message_delay_10,
    &message_sound_off,
    &message_blue_255,
    &message_delay_10,
    &message_blue_0,
    NULL,
};

static const NotificationSequence bl_seq_ping_close = {
    &message_vibro_on,
    &message_note_c7,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,
    &message_green_255,
    &message_delay_10,
    &message_green_0,
    NULL,
};

static uint32_t bl_track_ping_interval(int8_t rssi) {
    /* -45 dBm -> fastest, -100 dBm -> slowest */
    int32_t interval = BL_PING_MIN_MS + ((int32_t)(-45 - rssi) * 26);
    if(interval < BL_PING_MIN_MS) interval = BL_PING_MIN_MS;
    if(interval > BL_PING_MAX_MS) interval = BL_PING_MAX_MS;
    return (uint32_t)interval;
}

void ble_scene_track_on_enter(void* context) {
    BlApp* app = context;

    bl_view_track_reset(app->view_track);
    app->last_feedback = 0;

    /* Homing means walking with the Flipper in hand for minutes; the screen
     * dimming mid-sweep is exactly when the reading is wanted. */
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdTrack);
}

bool ble_scene_track_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeTick) return false;

    BlDeviceView device;
    if(!bl_scanner_get_view(app->scanner, app->target_mac, &device)) {
        /* The device fell out of the table entirely */
        memset(&device, 0, sizeof(device));
        memcpy(device.mac, app->target_mac, 6);
        snprintf(
            device.label,
            sizeof(device.label),
            "%02X%02X%02X%02X%02X%02X",
            app->target_mac[0],
            app->target_mac[1],
            app->target_mac[2],
            app->target_mac[3],
            app->target_mac[4],
            app->target_mac[5]);
        device.rssi = -100;
        device.rssi_peak = -100;
        device.score = -1;
        device.group_idx = -1;
        device.age_ms = BL_LOST_MS;
    }

    const bool present = device.age_ms < BL_LOST_MS;

    bl_view_track_update(
        app->view_track,
        &device,
        bl_app_group_name(app, device.group_idx),
        bl_app_distance_hint(device.rssi),
        present);

    if(present && (app->settings.sound || app->settings.vibro)) {
        const uint32_t now = furi_get_tick();
        const uint32_t interval = bl_track_ping_interval(device.rssi);

        if(now - app->last_feedback >= interval) {
            app->last_feedback = now;
            const bool very_close = device.rssi >= -55;
            if(very_close && app->settings.vibro) {
                notification_message(app->notifications, &bl_seq_ping_close);
            } else if(app->settings.sound) {
                notification_message(app->notifications, &bl_seq_ping);
            }
        }
    }

    return true;
}

void ble_scene_track_on_exit(void* context) {
    BlApp* app = context;
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}
