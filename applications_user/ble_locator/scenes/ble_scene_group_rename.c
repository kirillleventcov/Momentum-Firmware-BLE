#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <string.h>
#include <stdio.h>

static void ble_scene_group_rename_callback(void* context) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, BlCustomEventSelect);
}

static bool ble_scene_group_rename_validator(const char* text, FuriString* error, void* context) {
    BlApp* app = context;
    const char* problem = bl_group_name_problem(&app->groups, text, (int8_t)app->selected_group);
    if(problem) furi_string_set(error, problem);
    return problem == NULL;
}

void ble_scene_group_rename_on_enter(void* context) {
    BlApp* app = context;

    if(app->selected_group >= app->groups.count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    snprintf(
        app->text_buffer,
        sizeof(app->text_buffer),
        "%s",
        app->groups.items[app->selected_group].name);

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Rename group");
    text_input_set_validator(app->text_input, ble_scene_group_rename_validator, app);
    text_input_set_result_callback(
        app->text_input,
        ble_scene_group_rename_callback,
        app,
        app->text_buffer,
        sizeof(app->text_buffer),
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdTextInput);
}

bool ble_scene_group_rename_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != BlCustomEventSelect) return false;
    if(app->selected_group >= app->groups.count) return false;

    BlGroup* g = &app->groups.items[app->selected_group];
    bl_group_name_trim(app->text_buffer);

    /* The validator already refused blanks and clashes; this only guards
     * against the list having changed underneath the keyboard. */
    if(strncmp(g->name, app->text_buffer, BL_GROUP_NAME_MAX) != 0 &&
       bl_group_name_available(&app->groups, app->text_buffer, (int8_t)app->selected_group)) {
        /* The lock follows the group by name, so it has to be renamed too or
         * it would fall back to "all groups" on the next apply. */
        bool locked_here = strncmp(app->settings.group_lock, g->name, BL_GROUP_NAME_MAX) == 0;

        snprintf(g->name, sizeof(g->name), "%s", app->text_buffer);
        bl_app_save_groups(app);

        if(locked_here) {
            snprintf(app->settings.group_lock, sizeof(app->settings.group_lock), "%s", g->name);
            bl_app_save_settings(app);
        }
        bl_app_apply_groups(app);
    }

    scene_manager_previous_scene(app->scene_manager);
    return true;
}

void ble_scene_group_rename_on_exit(void* context) {
    BlApp* app = context;
    text_input_set_validator(app->text_input, NULL, NULL);
    text_input_reset(app->text_input);
}
