#pragma once

#include <gui/scene_manager.h>

/* Generate scene id enum */
#define ADD_SCENE(prefix, name, id) BlScene##id,
typedef enum {
#include "ble_scene_config.h"
    BlSceneNum,
} BlScene;
#undef ADD_SCENE

extern const SceneManagerHandlers ble_scene_handlers;

/* Generate scene on_enter handlers declaration */
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_enter(void*);
#include "ble_scene_config.h"
#undef ADD_SCENE

/* Generate scene on_event handlers declaration */
#define ADD_SCENE(prefix, name, id) \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event);
#include "ble_scene_config.h"
#undef ADD_SCENE

/* Generate scene on_exit handlers declaration */
#define ADD_SCENE(prefix, name, id) void prefix##_scene_##name##_on_exit(void* context);
#include "ble_scene_config.h"
#undef ADD_SCENE
