#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <stdio.h>

static void ble_scene_groups_callback(void* context, uint32_t index) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_scene_groups_on_enter(void* context) {
    BlApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Groups");

    char label[48];
    char summary[32];

    for(uint8_t i = 0; i < app->groups.count; i++) {
        const BlGroup* p = &app->groups.items[i];
        bl_group_describe(p, summary, sizeof(summary));
        snprintf(
            label,
            sizeof(label),
            "%s %s (%s)",
            p->enabled ? "[x]" : "[ ]",
            p->name,
            summary);
        submenu_add_item(app->submenu, label, i, ble_scene_groups_callback, app);
    }

    if(app->groups.count == 0) {
        submenu_add_item(
            app->submenu, "(none - learn one first)", 0xFE, ble_scene_groups_callback, app);
    }

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, BlSceneGroups));

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdSubmenu);
}

bool ble_scene_groups_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == 0xFE) return true;

    if(event.event < app->groups.count) {
        app->selected_group = (uint8_t)event.event;
        scene_manager_set_scene_state(app->scene_manager, BlSceneGroups, event.event);
        scene_manager_next_scene(app->scene_manager, BlSceneGroupEdit);
        return true;
    }

    return false;
}

void ble_scene_groups_on_exit(void* context) {
    BlApp* app = context;
    submenu_reset(app->submenu);
}
