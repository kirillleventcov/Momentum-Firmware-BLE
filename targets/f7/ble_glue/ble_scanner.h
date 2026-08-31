/**
 * @file ble_scanner.h
 * BLE observer (passive/active scanning) glue.
 *
 * Only usable when the "full" radio stack (stm32wb5x_BLE_Stack_full_fw.bin) is
 * installed - the "light" stack is peripheral-only and its link layer has no
 * scanner at all.
 */

#pragma once

#include <furi_hal_bt.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start the observer procedure.
 *
 * @param      config    scan parameters
 * @param      callback  called from the BLE event thread for every report
 * @param      context   user context passed to callback
 *
 * @return     true if the controller accepted the scan request
 */
bool ble_scanner_start(
    const FuriHalBtScanConfig* config,
    FuriHalBtScanCallback callback,
    void* context);

/** Stop the observer procedure.
 *
 * @return     true if scanning was stopped (or was not running)
 */
bool ble_scanner_stop(void);

/** @return    true while the observer procedure is running */
bool ble_scanner_is_active(void);

#ifdef __cplusplus
}
#endif
