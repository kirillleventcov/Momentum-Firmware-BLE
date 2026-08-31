#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <string.h>

static void bl_device_info_button_callback(
    GuiButtonType result,
    InputType type,
    void* context) {
    BlApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

static void bl_device_info_build_text(BlApp* app, const BlFeatures* f) {
    FuriString* text = app->text_box_store;

    bl_features_describe(f, text);

    /* Show how each group judges this device - the fastest way to work out
     * why something is (not) matching in the field. */
    furi_string_cat_str(text, "\nGroup scores:\n");
    for(uint8_t i = 0; i < app->groups.count; i++) {
        const int score = bl_group_score(&app->groups.items[i], f);
        furi_string_cat_printf(
            text,
            "%s%s: ",
            app->groups.items[i].enabled ? "" : "(off) ",
            app->groups.items[i].name);
        if(score < 0) {
            furi_string_cat_str(text, "no\n");
        } else {
            furi_string_cat_printf(text, "%d%%\n", score);
        }
    }
}

void ble_scene_device_info_on_enter(void* context) {
    BlApp* app = context;

    BlFeatures features;
    widget_reset(app->widget);

    if(!bl_scanner_get_features(app->scanner, app->target_mac, &features)) {
        widget_add_string_multiline_element(
            app->widget, 64, 30, AlignCenter, AlignCenter, FontSecondary, "Device gone\nout of range");
    } else {
        bl_device_info_build_text(app, &features);
        widget_add_text_scroll_element(
            app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text_box_store));

        if(app->scan_mode == BlScanModeLearn) {
            widget_add_button_element(
                app->widget,
                GuiButtonTypeCenter,
                "Capture",
                bl_device_info_button_callback,
                app);
        } else {
            widget_add_button_element(
                app->widget,
                GuiButtonTypeRight,
                "Track",
                bl_device_info_button_callback,
                app);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdWidget);
}

bool ble_scene_device_info_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == GuiButtonTypeRight) {
        scene_manager_next_scene(app->scene_manager, BlSceneTrack);
        return true;
    }

    if(event.event == GuiButtonTypeCenter) {
        /* The scan scene owns the popup used to report the outcome */
        scene_manager_previous_scene(app->scene_manager);
        view_dispatcher_send_custom_event(app->view_dispatcher, BlCustomEventCaptureTarget);
        return true;
    }

    return false;
}

void ble_scene_device_info_on_exit(void* context) {
    BlApp* app = context;
    widget_reset(app->widget);
}
