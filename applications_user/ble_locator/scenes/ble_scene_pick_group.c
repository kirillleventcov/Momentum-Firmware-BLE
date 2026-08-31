#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <stdio.h>
#include <string.h>

/* Index 0 is "Any group"; group i sits at index i + 1. */
#define BL_PICK_ANY   (0)
#define BL_PICK_EMPTY (0xFE)

static void bl_scene_pick_group_callback(void* context, uint32_t index) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_scene_pick_group_on_enter(void* context) {
    BlApp* app = context;

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Find a group");

    if(app->groups.count == 0) {
        submenu_add_item(
            app->submenu, "(none - learn one first)", BL_PICK_EMPTY, bl_scene_pick_group_callback,
            app);
        view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdSubmenu);
        return;
    }

    /* "Any group" is still a filter: it hides everything the app has never been
     * taught, it just does not care which group a hit belongs to. */
    submenu_add_item(
        app->submenu, "Any group", BL_PICK_ANY, bl_scene_pick_group_callback, app);

    char label[48];
    for(uint8_t i = 0; i < app->groups.count; i++) {
        const BlGroup* g = &app->groups.items[i];
        snprintf(label, sizeof(label), "%s%s", g->enabled ? "" : "(off) ", g->name);
        submenu_add_item(
            app->submenu, label, (uint32_t)i + 1, bl_scene_pick_group_callback, app);
    }

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, BlScenePickGroup));

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdSubmenu);
}

bool ble_scene_pick_group_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == BL_PICK_EMPTY) return true;

    if(event.event == BL_PICK_ANY) {
        app->settings.group_lock[0] = '\0';
    } else if(event.event <= app->groups.count) {
        snprintf(
            app->settings.group_lock,
            sizeof(app->settings.group_lock),
            "%s",
            app->groups.items[event.event - 1].name);
    } else {
        return false;
    }

    scene_manager_set_scene_state(app->scene_manager, BlScenePickGroup, event.event);
    bl_app_save_settings(app);

    app->scan_mode = BlScanModeGroup;
    app->settle_pending = true;
    scene_manager_next_scene(app->scene_manager, BlSceneScan);
    return true;
}

void ble_scene_pick_group_on_exit(void* context) {
    BlApp* app = context;
    submenu_reset(app->submenu);
}
