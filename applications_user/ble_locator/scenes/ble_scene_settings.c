#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <stdio.h>
#include <string.h>

static const char* const bl_sensitivity_names[BlSensitivityCount] = {
    "Strict",
    "Normal",
    "Loose",
};

static const char* const bl_on_off[2] = {"Off", "On"};

/* -100 means "no cut-off" */
static const int8_t bl_rssi_values[] = {-100, -90, -85, -80, -75, -70, -65, -60, -55, -50};

static void bl_settings_settle(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.settle_s = (uint8_t)(BL_SETTLE_MIN_S + index);

    char buf[8];
    snprintf(buf, sizeof(buf), "%u s", app->settings.settle_s);
    variable_item_set_current_value_text(item, buf);
    bl_app_save_settings(app);
}

static void bl_settings_sensitivity(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.sensitivity = index;
    variable_item_set_current_value_text(item, bl_sensitivity_names[index]);
    bl_app_save_settings(app);
    bl_app_apply_groups(app);
}

static void bl_settings_active_scan(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.active_scan = (index != 0);
    variable_item_set_current_value_text(item, index ? "Active" : "Passive");
    bl_app_save_settings(app);
}

static void bl_settings_rate(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.low_power = (index != 0);
    variable_item_set_current_value_text(item, index ? "Saver" : "Fast");
    bl_app_save_settings(app);
}

static void bl_settings_rssi(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.rssi_min = bl_rssi_values[index];

    char buf[12];
    if(index == 0) {
        snprintf(buf, sizeof(buf), "Any");
    } else {
        snprintf(buf, sizeof(buf), "%d dBm", bl_rssi_values[index]);
    }
    variable_item_set_current_value_text(item, buf);
    bl_app_save_settings(app);
}

static void bl_settings_sound(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.sound = (index != 0);
    variable_item_set_current_value_text(item, bl_on_off[index]);
    bl_app_save_settings(app);
}

static void bl_settings_vibro(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.vibro = (index != 0);
    variable_item_set_current_value_text(item, bl_on_off[index]);
    bl_app_save_settings(app);
}

static void bl_settings_log(VariableItem* item) {
    BlApp* app = variable_item_get_context(item);
    const uint8_t index = variable_item_get_current_value_index(item);
    app->settings.log_csv = (index != 0);
    variable_item_set_current_value_text(item, bl_on_off[index]);
    bl_app_save_settings(app);
}

void ble_scene_settings_on_enter(void* context) {
    BlApp* app = context;
    VariableItem* item;
    char buf[12];

    variable_item_list_reset(app->var_item_list);

    /* First, because it is the setting that most changes how the app feels. */
    item = variable_item_list_add(
        app->var_item_list,
        "Settle time",
        (uint8_t)(BL_SETTLE_MAX_S - BL_SETTLE_MIN_S + 1),
        bl_settings_settle,
        app);
    variable_item_set_current_value_index(
        item, (uint8_t)(app->settings.settle_s - BL_SETTLE_MIN_S));
    snprintf(buf, sizeof(buf), "%u s", app->settings.settle_s);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(
        app->var_item_list, "Match mode", BlSensitivityCount, bl_settings_sensitivity, app);
    variable_item_set_current_value_index(item, app->settings.sensitivity);
    variable_item_set_current_value_text(item, bl_sensitivity_names[app->settings.sensitivity]);

    item = variable_item_list_add(
        app->var_item_list, "Scan type", 2, bl_settings_active_scan, app);
    variable_item_set_current_value_index(item, app->settings.active_scan ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.active_scan ? "Active" : "Passive");

    item = variable_item_list_add(app->var_item_list, "Scan rate", 2, bl_settings_rate, app);
    variable_item_set_current_value_index(item, app->settings.low_power ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.low_power ? "Saver" : "Fast");

    uint8_t rssi_index = 0;
    for(uint8_t i = 0; i < COUNT_OF(bl_rssi_values); i++) {
        if(bl_rssi_values[i] == app->settings.rssi_min) rssi_index = i;
    }
    item = variable_item_list_add(
        app->var_item_list, "Min signal", COUNT_OF(bl_rssi_values), bl_settings_rssi, app);
    variable_item_set_current_value_index(item, rssi_index);
    if(rssi_index == 0) {
        snprintf(buf, sizeof(buf), "Any");
    } else {
        snprintf(buf, sizeof(buf), "%d dBm", bl_rssi_values[rssi_index]);
    }
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(app->var_item_list, "Beeper", 2, bl_settings_sound, app);
    variable_item_set_current_value_index(item, app->settings.sound ? 1 : 0);
    variable_item_set_current_value_text(item, bl_on_off[app->settings.sound ? 1 : 0]);

    item = variable_item_list_add(app->var_item_list, "Vibration", 2, bl_settings_vibro, app);
    variable_item_set_current_value_index(item, app->settings.vibro ? 1 : 0);
    variable_item_set_current_value_text(item, bl_on_off[app->settings.vibro ? 1 : 0]);

    item = variable_item_list_add(app->var_item_list, "Survey log", 2, bl_settings_log, app);
    variable_item_set_current_value_index(item, app->settings.log_csv ? 1 : 0);
    variable_item_set_current_value_text(item, bl_on_off[app->settings.log_csv ? 1 : 0]);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdVarItemList);
}

bool ble_scene_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void ble_scene_settings_on_exit(void* context) {
    BlApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
