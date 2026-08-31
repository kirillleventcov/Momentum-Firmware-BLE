#pragma once
#include <furi.h>

typedef enum {
    FuriHalBtAdvEventTypeConnectableUndirected = 0x00,
    FuriHalBtAdvEventTypeConnectableDirected = 0x01,
    FuriHalBtAdvEventTypeScannableUndirected = 0x02,
    FuriHalBtAdvEventTypeNonConnectableUndirected = 0x03,
    FuriHalBtAdvEventTypeScanResponse = 0x04,
} FuriHalBtAdvEventType;

typedef struct {
    uint8_t event_type;
    uint8_t address_type;
    uint8_t address[6];
    int8_t rssi;
    uint8_t data_len;
    const uint8_t* data;
} FuriHalBtAdvReport;
