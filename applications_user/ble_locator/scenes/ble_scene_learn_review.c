#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <stdio.h>

static void
    bl_learn_review_button(GuiButtonType result, InputType type, void* context) {
    BlApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

void ble_scene_learn_review_on_enter(void* context) {
    BlApp* app = context;
    const BlBuildReport* r = &app->build_report;

    char summary[40];
    bl_group_describe(&app->pending_group, summary, sizeof(summary));

    FuriString* text = app->text_box_store;
    furi_string_reset(text);

    furi_string_cat_printf(text, "Keys on: %s\n", summary);
    /* Same numbers either way, but "member" is the word that makes sense when
     * the group already existed and you just added to it. */
    const char* noun = (app->pending_kind == BlPendingEdit) ? "member" : "capture";
    furi_string_cat_printf(
        text, "From %u %s%s\n\n", r->captures, noun, r->captures == 1 ? "" : "s");

    if(r->others_tested == 0) {
        furi_string_cat_str(
            text,
            "Nothing else was on air to\n"
            "compare against, so this\n"
            "group is untested. Check\n"
            "it somewhere busier before\n"
            "trusting it.\n");
    } else if(r->separated) {
        furi_string_cat_printf(
            text,
            "SELECTIVE\n"
            "0 of %u other devices\n"
            "around you match.\n\n",
            r->others_tested);
        furi_string_cat_str(
            text,
            "Find a group will show\n"
            "this group and nothing\n"
            "else.\n");
    } else {
        furi_string_cat_printf(
            text,
            "TOO BROAD\n"
            "%u of %u other devices\n"
            "around you also match.\n\n",
            r->others_matched,
            r->others_tested);
        furi_string_cat_str(
            text,
            "These units advertise\n"
            "nothing that separates\n"
            "them from other gear\n"
            "nearby, so finding this\n"
            "group lists that gear\n"
            "too.\n\n"
            "Back out and capture a\n"
            "different mix of units,\n"
            "or learn somewhere with\n"
            "less around.\n");
    }

    furi_string_cat_printf(text, "\nMatch threshold: %u%%\n", app->pending_group.threshold);

    widget_reset(app->widget);
    widget_add_text_scroll_element(
        app->widget, 0, 0, 128, 52, furi_string_get_cstr(app->text_box_store));
    widget_add_button_element(
        app->widget, GuiButtonTypeCenter, "Save", bl_learn_review_button, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdWidget);
}

bool ble_scene_learn_review_on_event(void* context, SceneManagerEvent event) {
    BlApp* app = context;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == GuiButtonTypeCenter) {
        if(app->pending_kind == BlPendingEdit) {
            bl_app_commit_group(app);
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, BlSceneGroupEdit);
        } else {
            scene_manager_next_scene(app->scene_manager, BlSceneLearnName);
        }
        return true;
    }

    return false;
}

void ble_scene_learn_review_on_exit(void* context) {
    BlApp* app = context;
    widget_reset(app->widget);
}
