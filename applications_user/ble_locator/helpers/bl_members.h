/**
 * @file bl_members.h
 * The devices a group was built from, kept so the group can be edited later.
 *
 * A group's fingerprint is derived - it is the intersection of the devices that
 * went into it - so the only way to add a ninth device next week, or drop one
 * that turned out not to belong, is to still have the other members around to
 * re-intersect. Learning in one sitting was never the point; it was just all
 * the app could do while it threw the captures away.
 *
 * Members live one file per group rather than inside groups.txt because eight
 * of them is 1280 bytes and twenty-four groups' worth would not fit in RAM.
 * They are loaded only while a group is being edited.
 *
 * What is stored is the advertisement itself, not the parsed feature set, so
 * loading runs the same parser as live radio does and cannot drift from it.
 */

#pragma once

#include "bl_group.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Build the member-file path for group `id` under `folder`. */
void bl_members_path(const char* folder, uint16_t id, char* out, size_t out_size);

/** Read a group's members. A missing file is not an error: the set comes back
 * empty, which is what a group learned before members were kept looks like. */
bool bl_members_load(BlCaptureSet* set, const char* path);

bool bl_members_save(const BlCaptureSet* set, const char* path);

void bl_members_remove(const char* path);

#ifdef __cplusplus
}
#endif
