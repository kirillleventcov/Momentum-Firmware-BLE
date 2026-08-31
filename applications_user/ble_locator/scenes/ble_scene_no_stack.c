#include "../ble_locator_i.h"
#include "ble_scene.h"

#include <furi_hal_bt.h>

static const char* const bl_no_stack_text =
    "BLE scanning unavailable\n"
    "\n"
    "This Flipper is running the\n"
    "\"light\" BLE co-processor stack.\n"
    "It is peripheral-only: the radio\n"
    "can advertise but it physically\n"
    "cannot receive advertisements,\n"
    "so no scanner can work on it.\n"
    "\n"
    "FIX\n"
    "Flash a firmware build that\n"
    "bundles the full radio stack:\n"
    "\n"
    "  fbt_options.py\n"
    "  COPRO_STACK_BIN =\n"
    "   stm32wb5x_BLE_Stack_full_fw.bin\n"
    "  COPRO_STACK_TYPE = \"ble_full\"\n"
    "\n"
    "then ./fbt updater_package and\n"
    "install it. The updater swaps the\n"
    "radio stack for you; it takes an\n"
    "extra reboot or two.\n"
    "\n"
    "Check the installed stack under\n"
    "Settings > System > About >\n"
    "Firmware (radio stack type), or\n"
    "over the CLI with \"bt info\".\n";

void ble_scene_no_stack_on_enter(void* context) {
    BlApp* app = context;

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, bl_no_stack_text);

    view_dispatcher_switch_to_view(app->view_dispatcher, BlViewIdWidget);
}

bool ble_scene_no_stack_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void ble_scene_no_stack_on_exit(void* context) {
    BlApp* app = context;
    widget_reset(app->widget);
}
