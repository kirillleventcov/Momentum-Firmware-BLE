#pragma once

#include "../helpers/bl_scanner.h"

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlViewTrack BlViewTrack;

BlViewTrack* bl_view_track_alloc(void);
void bl_view_track_free(BlViewTrack* view_track);
View* bl_view_track_get_view(BlViewTrack* view_track);

/** Push a fresh reading. `present` false means the device went quiet. */
void bl_view_track_update(
    BlViewTrack* view_track,
    const BlDeviceView* device,
    const char* group_name,
    const char* distance_hint,
    bool present);

void bl_view_track_reset(BlViewTrack* view_track);

#ifdef __cplusplus
}
#endif
