#include "bl_order.h"

#include <string.h>

void bl_order_freeze(BlRowOrder* order, const BlDeviceView* rows, size_t count) {
    if(count > BL_SNAPSHOT_MAX) count = BL_SNAPSHOT_MAX;
    order->count = (uint8_t)count;
    for(size_t i = 0; i < count; i++) {
        memcpy(order->mac[i], rows[i].mac, 6);
    }
}

size_t bl_order_apply(BlRowOrder* order, BlDeviceView* rows, size_t count) {
    /* Only an index permutation goes on the stack. BL_SNAPSHOT_MAX rows of
     * BlDeviceView is far more than the app thread should be carrying. */
    uint8_t seq[BL_SNAPSHOT_MAX];
    bool used[BL_SNAPSHOT_MAX];
    size_t out = 0;

    if(count > BL_SNAPSHOT_MAX) count = BL_SNAPSHOT_MAX;
    memset(used, 0, sizeof(used));

    /* 1. the frozen order, skipping entries that are no longer present */
    for(uint8_t i = 0; i < order->count; i++) {
        for(size_t j = 0; j < count; j++) {
            if(used[j]) continue;
            if(memcmp(rows[j].mac, order->mac[i], 6) != 0) continue;
            seq[out++] = (uint8_t)j;
            used[j] = true;
            break;
        }
    }

    /* 2. newcomers, each dropped in at the position its signal earns */
    for(size_t j = 0; j < count; j++) {
        if(used[j]) continue;
        size_t pos = out;
        for(size_t k = 0; k < out; k++) {
            if(rows[j].rssi > rows[seq[k]].rssi) {
                pos = k;
                break;
            }
        }
        for(size_t k = out; k > pos; k--) seq[k] = seq[k - 1];
        seq[pos] = (uint8_t)j;
        out++;
        used[j] = true;
    }

    /* 3. apply the permutation in place. Swapping rows[i] with rows[j] moves
     * whatever was at i out to j, so any later index still pointing at i has
     * to follow it. */
    for(size_t i = 0; i < out; i++) {
        const size_t j = seq[i];
        if(j == i) continue;
        const BlDeviceView tmp = rows[i];
        rows[i] = rows[j];
        rows[j] = tmp;
        for(size_t k = i + 1; k < out; k++) {
            if(seq[k] == i) {
                seq[k] = (uint8_t)j;
                break;
            }
        }
        seq[i] = (uint8_t)i;
    }

    bl_order_freeze(order, rows, out);
    return out;
}
