#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <stdio.h>

#define BL_MEMBERS_EMPTY (0xFE)

static void ble_scene_group_members_callback(void* context, uint32_t index) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_scene_group_members_on_enter(void* context) {
    BlApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Members");

    char label[44];

    for(uint8_t i = 0; i < app->captures.count; i++) {
        char name[BL_NAME_MAX + 1];
        bl_features_label(&app->captures.items[i], name, sizeof(name));
        snprintf(label, sizeof(label), "%u. %s", i + 1, name);
        submenu_add_item(app->submenu, label, i, ble_scene_group_members_callback, app);
    }

    if(app->captures.count == 0) {
        /* Either a group built before members were kept, or one whose file has
         * gone missing. Either way its fingerprint still works; it just cannot
         * be edited without starting its member list over. */
        submenu_add_item(
            app->submenu,
            "(none stored)",
            BL_MEMBERS_EMPTY,
            ble_scene_group_members_callback,
            app);
    }

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, BlSceneGroupMembers));

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdSubmenu);
}

bool ble_scene_group_members_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == BL_MEMBERS_EMPTY) return true;

    if(event.event < app->captures.count) {
        app->selected_member = (uint8_t)event.event;
        scene_manager_set_scene_state(app->scene_manager, BlSceneGroupMembers, event.event);
        scene_manager_next_scene(app->scene_manager, BlSceneGroupMemberInfo);
        return true;
    }

    return false;
}

void ble_scene_group_members_on_exit(void* context) {
    BlApp* app = context;
    submenu_reset(app->submenu);
}
