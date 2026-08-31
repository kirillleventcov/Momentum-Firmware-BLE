#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <string.h>

static void
    bl_member_info_button(GuiButtonType result, InputType type, void* context) {
    BlApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void ble_scene_group_member_info_on_enter(void* context) {
    BlApp* app = context;

    if(app->selected_member >= app->captures.count) {
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    bl_features_describe(&app->captures.items[app->selected_member], app->text_box_store);

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text_box_store));
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Remove", bl_member_info_button, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdWidget);
}

bool ble_scene_group_member_info_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event != GuiButtonTypeRight) return false;
    if(app->selected_member >= app->captures.count) return false;

    if(app->captures.count == 1) {
        bl_app_show_message(app, BlViewIdWidget, "Cannot remove", "A group needs at least\none member");
        return true;
    }

    /* Take the member out, see whether what is left still intersects into a
     * usable fingerprint, and put it back if it does not. */
    const BlFeatures removed = app->captures.items[app->selected_member];
    const uint8_t at = app->selected_member;

    for(uint8_t i = at; i + 1 < app->captures.count; i++) {
        app->captures.items[i] = app->captures.items[i + 1];
    }
    app->captures.count--;

    const char* error = NULL;
    if(!bl_app_rebuild_group(app, &error)) {
        for(uint8_t i = app->captures.count; i > at; i--) {
            app->captures.items[i] = app->captures.items[i - 1];
        }
        app->captures.items[at] = removed;
        app->captures.count++;
        bl_app_show_message(app, BlViewIdWidget, "Cannot remove", error ? error : "Unknown problem");
        return true;
    }

    scene_manager_next_scene(app->scene_manager, BlSceneLearnReview);
    return true;
}

void ble_scene_group_member_info_on_exit(void* context) {
    BlApp* app = context;
    widget_reset(app->widget);
}
