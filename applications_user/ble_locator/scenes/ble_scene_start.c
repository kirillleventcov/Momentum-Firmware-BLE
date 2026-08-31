#include "../ble_locator_i.h"
#include "ble_scene.h"

typedef enum {
    BlStartIndexScanAll,
    BlStartIndexFindGroup,
    BlStartIndexLearn,
    BlStartIndexGroups,
    BlStartIndexSettings,
    BlStartIndexAbout,
} BlStartIndex;

static void bl_scene_start_callback(void* context, uint32_t index) {
    BlApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void ble_scene_start_on_enter(void* context) {
    BlApp* app = context;

    /* Every path back to the menu should leave the radio idle. */
    bl_app_scan_stop(app);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "BLE Locator");

    /* The first two entries are the whole point of the app: look at everything,
     * or look at one thing only. Which one you are in is an explicit choice
     * made here, not a setting buried three screens away. */
    submenu_add_item(
        app->submenu, "Scan all devices", BlStartIndexScanAll, bl_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Find a group", BlStartIndexFindGroup, bl_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Learn a group", BlStartIndexLearn, bl_scene_start_callback, app);
    submenu_add_item(app->submenu, "Groups", BlStartIndexGroups, bl_scene_start_callback, app);
    submenu_add_item(
        app->submenu, "Settings", BlStartIndexSettings, bl_scene_start_callback, app);
    submenu_add_item(app->submenu, "About", BlStartIndexAbout, bl_scene_start_callback, app);

    submenu_set_selected_item(
        app->submenu, scene_manager_get_scene_state(app->scene_manager, BlSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdSubmenu);
}

bool ble_scene_start_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    scene_manager_set_scene_state(app->scene_manager, BlSceneStart, event.event);

    switch(event.event) {
    case BlStartIndexScanAll:
        app->scan_mode = BlScanModeAll;
        app->settle_pending = true;
        scene_manager_next_scene(app->scene_manager, BlSceneScan);
        return true;

    case BlStartIndexFindGroup:
        scene_manager_next_scene(app->scene_manager, BlScenePickGroup);
        return true;

    case BlStartIndexLearn:
        app->scan_mode = BlScanModeLearn;
        app->captures.count = 0;
        app->settle_pending = true;
        scene_manager_next_scene(app->scene_manager, BlSceneScan);
        return true;

    case BlStartIndexGroups:
        scene_manager_next_scene(app->scene_manager, BlSceneGroups);
        return true;

    case BlStartIndexSettings:
        scene_manager_next_scene(app->scene_manager, BlSceneSettings);
        return true;

    case BlStartIndexAbout:
        scene_manager_next_scene(app->scene_manager, BlSceneAbout);
        return true;

    default:
        return false;
    }
}

void ble_scene_start_on_exit(void* context) {
    BlApp* app = context;
    submenu_reset(app->submenu);
}
