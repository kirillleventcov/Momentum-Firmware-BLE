/**
 * @file bl_order.h
 * Frozen row order for the device list.
 *
 * Sorting live by signal strength makes the list impossible to aim at: rows
 * swap places between ticks and the row you were reaching for is gone. So the
 * order is decided once, when the settle window closes, and then held. Signal
 * numbers keep updating underneath; only the positions stop moving.
 */

#pragma once

#include "bl_scanner.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Display order, held as MACs so it survives devices dropping out and
 * coming back rather than being invalidated by every table reshuffle. */
typedef struct {
    uint8_t mac[BL_SNAPSHOT_MAX][6];
    uint8_t count;
} BlRowOrder;

/** Drop the order; the next bl_order_apply() will take whatever it is given. */
static inline void bl_order_reset(BlRowOrder* order) {
    order->count = 0;
}

/** Adopt `rows` as the order. Call once with a signal-sorted snapshot, which
 * is what makes the frozen list read "closest at the top". */
void bl_order_freeze(BlRowOrder* order, const BlDeviceView* rows, size_t count);

/** Reorder `rows` in place to match `order`, then update `order` to match.
 *
 * Rows already in the order keep their slot. Rows that have aged out drop out
 * of it. Rows that are new are inserted, which shifts everything below them by
 * one - the one bit of movement worth accepting, because a device you have just
 * walked into range of is the whole point of carrying the thing.
 *
 * A newcomer goes **immediately above the first row weaker than it**. Note that
 * a held order is not a sorted one - walk far enough and it will not be - so
 * there is no position that is right by every comparison. This rule places a
 * newcomer as high as its signal can justify, which is the safe direction to be
 * wrong in: a device that has just come into range is worth seeing, and burying
 * it below rows it is stronger than would hide it.
 *
 * `rows` is expected sorted strongest-first, which decides how equal newcomers
 * arriving in the same tick land relative to each other.
 *
 * @return number of rows written to `rows` (never more than BL_SNAPSHOT_MAX)
 */
size_t bl_order_apply(BlRowOrder* order, BlDeviceView* rows, size_t count);

#ifdef __cplusplus
}
#endif
