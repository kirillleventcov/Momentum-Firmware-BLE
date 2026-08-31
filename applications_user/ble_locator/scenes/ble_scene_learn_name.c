#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <string.h>
#include <stdio.h>

static const NotificationSequence bl_seq_saved = {
    &message_vibro_on,
    &message_delay_50,
    &message_vibro_off,
    &message_note_c5,
    &message_delay_50,
    &message_note_g5,
    &message_delay_50,
    &message_sound_off,
    &message_green_255,
    &message_delay_100,
    &message_green_0,
    NULL,
};

static const NotificationSequence bl_seq_failed = {
    &message_note_c4,
    &message_delay_100,
    &message_sound_off,
    &message_red_255,
    &message_delay_100,
    &message_red_0,
    NULL,
};

static void ble_scene_learn_name_callback(void* context) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, BlCustomEventSelect);
}

void ble_scene_learn_name_on_enter(void* context) {
    BlApp* app = context;

    snprintf(app->text_buffer, sizeof(app->text_buffer), "Group %u", app->groups.count + 1);

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name this group");
    text_input_set_result_callback(
        app->text_input,
        ble_scene_learn_name_callback,
        app,
        app->text_buffer,
        sizeof(app->text_buffer),
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdTextInput);
}

bool ble_scene_learn_name_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != BlCustomEventSelect) return false;

    snprintf(
        app->pending_group.name, sizeof(app->pending_group.name), "%s", app->text_buffer);
    app->pending_group.builtin = false;
    app->pending_group.enabled = true;
    app->pending_group.id = bl_group_list_next_id(&app->groups);

    if(bl_group_list_add(&app->groups, &app->pending_group)) {
        /* Keep what it was built from, so it can be added to next week. */
        for(uint8_t i = 0; i < app->groups.count; i++) {
            if(strncmp(app->groups.items[i].name, app->pending_group.name, BL_GROUP_NAME_MAX) ==
               0) {
                app->pending_group.id = app->groups.items[i].id;
                bl_app_save_members(app, i);
                break;
            }
        }
        bl_app_save_groups(app);
        bl_app_apply_groups(app);
        app->captures.count = 0;
        notification_message(app->notifications, &bl_seq_saved);
        /* Straight into finding with the fresh group, pointed at just that
         * group - checking it actually singles the devices out is the first
         * thing anyone wants to do after learning one. */
        snprintf(
            app->settings.group_lock,
            sizeof(app->settings.group_lock),
            "%s",
            app->pending_group.name);
        bl_app_save_settings(app);
        bl_app_apply_groups(app);
        app->scan_mode = BlScanModeGroup;
        app->settle_pending = true;
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, BlSceneScan);
    } else {
        notification_message(app->notifications, &bl_seq_failed);
        scene_manager_search_and_switch_to_previous_scene(app->scene_manager, BlSceneStart);
    }

    return true;
}

void ble_scene_learn_name_on_exit(void* context) {
    BlApp* app = context;
    text_input_reset(app->text_input);
}
