#include "../ble_locator_i.h"
#include "ble_scene.h"

#define BL_THRESHOLD_STEP (5)
#define BL_THRESHOLD_MIN  (10)
#define BL_THRESHOLD_MAX  (100)

static void bl_group_details_button(GuiButtonType result, InputType type, void* context) {
    BlApp* app = context;
    if(type != InputTypeShort && type != InputTypeRepeat) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

static void ble_scene_group_details_show(BlApp* app) {
    bl_group_details(&app->groups.items[app->selected_group], app->text_box_store);

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text_box_store));
    /* The one knob worth having per group. Match mode shifts every group at
     * once; this is for the single group that is too loose or too tight. */
    widget_add_button_element(app->widget, GuiButtonTypeLeft, "-5%", bl_group_details_button, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "+5%", bl_group_details_button, app);
}

void ble_scene_group_details_on_enter(void* context) {
    BlApp* app = context;

    if(app->selected_group >= app->groups.count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    /* Scene state doubles as the dirty flag: written to disk once on exit
     * rather than on every press. */
    scene_manager_set_scene_state(app->scene_manager, BlSceneGroupDetails, 0);
    ble_scene_group_details_show(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdWidget);
}

bool ble_scene_group_details_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(app->selected_group >= app->groups.count) return false;

    BlGroup* g = &app->groups.items[app->selected_group];
    int threshold = g->threshold;

    if(event.event == GuiButtonTypeLeft) {
        threshold -= BL_THRESHOLD_STEP;
    } else if(event.event == GuiButtonTypeRight) {
        threshold += BL_THRESHOLD_STEP;
    } else {
        return false;
    }

    if(threshold < BL_THRESHOLD_MIN) threshold = BL_THRESHOLD_MIN;
    if(threshold > BL_THRESHOLD_MAX) threshold = BL_THRESHOLD_MAX;
    if(threshold == g->threshold) return true;

    g->threshold = (uint8_t)threshold;
    scene_manager_set_scene_state(app->scene_manager, BlSceneGroupDetails, 1);
    ble_scene_group_details_show(app);
    return true;
}

void ble_scene_group_details_on_exit(void* context) {
    BlApp* app = context;

    if(scene_manager_get_scene_state(app->scene_manager, BlSceneGroupDetails)) {
        bl_app_save_groups(app);
        bl_app_apply_groups(app);
    }
    widget_reset(app->widget);
}
