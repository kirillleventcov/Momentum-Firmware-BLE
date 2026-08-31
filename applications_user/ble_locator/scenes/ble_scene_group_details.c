#include "../ble_locator_i.h"
#include "ble_scene.h"

void ble_scene_group_details_on_enter(void* context) {
    BlApp* app = context;

    if(app->selected_group >= app->groups.count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    bl_group_details(&app->groups.items[app->selected_group], app->text_box_store);

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 64, furi_string_get_cstr(app->text_box_store));

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdWidget);
}

bool ble_scene_group_details_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void ble_scene_group_details_on_exit(void* context) {
    BlApp* app = context;
    widget_reset(app->widget);
}
