#include "ble_scanner.h"

#include "app_common.h"
#include "furi_ble/event_dispatcher.h"

#include <ble/ble.h>
#include <furi.h>
#include <furi_hal_bt.h>

#define TAG "BleScanner"

typedef struct {
    GapSvcEventHandler* handler;
    FuriHalBtScanCallback callback;
    void* context;
    FuriMutex* mutex;
    bool active;
    bool via_hci;
} BleScanner;

static BleScanner ble_scanner = {0};

/* Parses HCI_LE_ADVERTISING_REPORT payloads.
 *
 * The STM32WB controller emits the reports interleaved (one full report after
 * another) rather than as per-field arrays, and always with Num_Reports == 1.
 * We follow the layout used by the ST stack itself
 * (hci_le_advertising_report_event_process in ble_events.c) but stay tolerant
 * of Num_Reports > 1.
 */
static void ble_scanner_process_adv_report(const uint8_t* payload, uint8_t payload_len) {
    if(payload_len < 1) {
        return;
    }

    const uint8_t num_reports = payload[0];
    const uint8_t* pos = payload + 1;
    const uint8_t* end = payload + payload_len;

    for(uint8_t i = 0; i < num_reports; i++) {
        /* Event_Type(1) Address_Type(1) Address(6) Length_Data(1) = 9 bytes */
        if(pos + 9 > end) {
            break;
        }

        const uint8_t data_len = pos[8];
        /* 9 header bytes + data + RSSI */
        if(pos + 9 + data_len + 1 > end) {
            break;
        }

        FuriHalBtAdvReport report = {
            .event_type = pos[0],
            .address_type = pos[1],
            .rssi = (int8_t)pos[9 + data_len],
            .data_len = data_len,
            .data = &pos[9],
        };
        memcpy(report.address, &pos[2], sizeof(report.address));

        /* Snapshot both so a concurrent stop cannot hand us a stale context */
        FuriHalBtScanCallback callback = ble_scanner.callback;
        void* callback_context = ble_scanner.context;
        if(callback) {
            callback(&report, callback_context);
        }

        pos += 9 + data_len + 1;
    }
}

static BleEventAckStatus ble_scanner_event_handler(void* event, void* context) {
    UNUSED(context);

    const hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);
    if(event_pckt->evt != HCI_LE_META_EVT_CODE) {
        return BleEventNotAck;
    }

    const evt_le_meta_event* meta_evt = (evt_le_meta_event*)event_pckt->data;
    if(meta_evt->subevent != HCI_LE_ADVERTISING_REPORT_SUBEVT_CODE) {
        return BleEventNotAck;
    }

    /* plen covers the whole meta event, subevent byte included */
    const uint8_t payload_len = (event_pckt->plen > 0) ? (event_pckt->plen - 1) : 0;
    ble_scanner_process_adv_report(meta_evt->data, payload_len);

    return BleEventAckFlowEnable;
}

static bool ble_scanner_controller_start(const FuriHalBtScanConfig* config) {
    const uint8_t scan_type = config->active_scan ? 0x01 : 0x00;
    const uint8_t filter_duplicates = config->filter_duplicates ? 0x01 : 0x00;

    /* Preferred path: GAP observation procedure. Requires the GAP layer to have
     * been initialized with the observer role (see gap_init()). */
    tBleStatus status = aci_gap_start_observation_proc(
        config->scan_interval,
        config->scan_window,
        scan_type,
        0x00, /* own address type: public */
        filter_duplicates,
        0x00 /* unfiltered scanning filter policy */);

    if(status == BLE_STATUS_SUCCESS) {
        ble_scanner.via_hci = false;
        return true;
    }

    FURI_LOG_W(TAG, "GAP observation proc failed (%02X), falling back to raw HCI", status);

    /* Fallback: plain HCI. Works even when aci_gap_init() was never called,
     * e.g. when no BLE profile is running. */
    status = hci_le_set_scan_parameters(
        scan_type, config->scan_interval, config->scan_window, 0x00, 0x00);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "hci_le_set_scan_parameters failed: %02X", status);
        return false;
    }

    status = hci_le_set_scan_enable(0x01, filter_duplicates);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "hci_le_set_scan_enable failed: %02X", status);
        return false;
    }

    ble_scanner.via_hci = true;
    return true;
}

static void ble_scanner_controller_stop(void) {
    if(ble_scanner.via_hci) {
        hci_le_set_scan_enable(0x00, 0x00);
    } else {
        tBleStatus status = aci_gap_terminate_gap_proc(GAP_OBSERVATION_PROC);
        if(status != BLE_STATUS_SUCCESS) {
            FURI_LOG_W(TAG, "Terminate observation proc: %02X", status);
            /* Belt and braces - make sure the radio really goes quiet */
            hci_le_set_scan_enable(0x00, 0x00);
        }
    }
}

static void ble_scanner_ensure_mutex(void) {
    if(!ble_scanner.mutex) {
        ble_scanner.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    }
}

bool ble_scanner_start(
    const FuriHalBtScanConfig* config,
    FuriHalBtScanCallback callback,
    void* context) {
    furi_check(config);
    furi_check(callback);

    if(furi_hal_bt_get_radio_stack() != FuriHalBtStackFull) {
        FURI_LOG_E(TAG, "Scanning needs the full radio stack");
        return false;
    }

    if(!ble_glue_is_radio_stack_ready()) {
        FURI_LOG_E(TAG, "Radio stack is not running");
        return false;
    }

    ble_scanner_ensure_mutex();
    furi_check(furi_mutex_acquire(ble_scanner.mutex, FuriWaitForever) == FuriStatusOk);

    bool started = false;
    do {
        if(ble_scanner.active) {
            break;
        }

        ble_scanner.callback = callback;
        ble_scanner.context = context;
        ble_scanner.handler =
            ble_event_dispatcher_register_svc_handler(ble_scanner_event_handler, &ble_scanner);

        if(!ble_scanner_controller_start(config)) {
            ble_event_dispatcher_unregister_svc_handler(ble_scanner.handler);
            ble_scanner.handler = NULL;
            ble_scanner.callback = NULL;
            ble_scanner.context = NULL;
            break;
        }

        ble_scanner.active = true;
        started = true;
    } while(false);

    furi_mutex_release(ble_scanner.mutex);
    return started;
}

bool ble_scanner_stop(void) {
    ble_scanner_ensure_mutex();
    furi_check(furi_mutex_acquire(ble_scanner.mutex, FuriWaitForever) == FuriStatusOk);

    if(ble_scanner.active) {
        ble_scanner_controller_stop();
        ble_scanner.active = false;

        /* Drop the callback before unregistering so a report already queued in
         * the HCI event buffer cannot reach a context the caller is about to
         * free, then give the BLE event thread a moment to drain what is left
         * before the handler node disappears from under it. */
        ble_scanner.callback = NULL;
        ble_scanner.context = NULL;
        furi_delay_ms(20);

        if(ble_scanner.handler) {
            ble_event_dispatcher_unregister_svc_handler(ble_scanner.handler);
            ble_scanner.handler = NULL;
        }
    }

    furi_mutex_release(ble_scanner.mutex);
    return true;
}

bool ble_scanner_is_active(void) {
    return ble_scanner.active;
}
