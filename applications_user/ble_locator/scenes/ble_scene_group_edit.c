#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <stdio.h>

typedef enum {
    BlGroupEditAdd,
    BlGroupEditMembers,
    BlGroupEditDetails,
    BlGroupEditToggle,
    BlGroupEditDelete,
} BlGroupEditIndex;

static void ble_scene_group_edit_callback(void* context, uint32_t index) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_scene_group_edit_on_enter(void* context) {
    BlApp* app = context;

    if(app->selected_group >= app->groups.count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    const BlGroup* g = &app->groups.items[app->selected_group];

    /* The member set is the group, as far as editing is concerned - everything
     * on this screen either adds to it, removes from it or reports on it. */
    bl_app_load_members(app, app->selected_group);

    char label[40];

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, g->name);

    submenu_add_item(
        app->submenu, "Add device", BlGroupEditAdd, ble_scene_group_edit_callback, app);

    snprintf(label, sizeof(label), "Members (%u)", app->captures.count);
    submenu_add_item(
        app->submenu, label, BlGroupEditMembers, ble_scene_group_edit_callback, app);

    submenu_add_item(
        app->submenu, "Details", BlGroupEditDetails, ble_scene_group_edit_callback, app);
    submenu_add_item(
        app->submenu,
        g->enabled ? "Disable" : "Enable",
        BlGroupEditToggle,
        ble_scene_group_edit_callback,
        app);
    submenu_add_item(
        app->submenu, "Delete group", BlGroupEditDelete, ble_scene_group_edit_callback, app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, BlSceneGroupEdit));

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdSubmenu);
}

bool ble_scene_group_edit_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(app->selected_group >= app->groups.count) return false;

    scene_manager_set_scene_state(app->scene_manager, BlSceneGroupEdit, event.event);

    switch(event.event) {
    case BlGroupEditAdd:
        /* Straight into a normal scan, showing everything - the device you are
         * adding is by definition one the group does not match yet. */
        app->scan_mode = BlScanModeAddMember;
        app->settle_pending = true;
        scene_manager_next_scene(app->scene_manager, BlSceneScan);
        return true;

    case BlGroupEditMembers:
        scene_manager_next_scene(app->scene_manager, BlSceneGroupMembers);
        return true;

    case BlGroupEditDetails:
        scene_manager_next_scene(app->scene_manager, BlSceneGroupDetails);
        return true;

    case BlGroupEditToggle: {
        BlGroup* g = &app->groups.items[app->selected_group];
        g->enabled = !g->enabled;
        bl_app_save_groups(app);
        bl_app_apply_groups(app);
        ble_scene_group_edit_on_enter(app);
        return true;
    }

    case BlGroupEditDelete:
        bl_app_delete_members(app, app->selected_group);
        bl_group_list_remove(&app->groups, app->selected_group);
        bl_app_save_groups(app);
        bl_app_apply_groups(app);
        scene_manager_set_scene_state(app->scene_manager, BlSceneGroups, 0);
        scene_manager_previous_scene(app->scene_manager);
        return true;

    default:
        return false;
    }
}

void ble_scene_group_edit_on_exit(void* context) {
    BlApp* app = context;
    submenu_reset(app->submenu);
}
