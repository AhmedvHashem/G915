#pragma once

#include <stdint.h>

#include "core/keyboard_state.h"

typedef enum {
    HOG_CLIENT_STARTING,
    HOG_CLIENT_UNPROVISIONED,
    HOG_CLIENT_SCANNING,
    HOG_CLIENT_CONNECTING,
    HOG_CLIENT_CANCELING_CONNECT,
    HOG_CLIENT_SECURING,
    HOG_CLIENT_HIDS_SETUP,
    HOG_CLIENT_READY,
    HOG_CLIENT_DISCONNECTING,
    HOG_CLIENT_BACKOFF,
} hog_client_state_t;

typedef struct {
    void (*keyboard_state_changed)(const keyboard_state_t *state,
                                   uint32_t epoch, void *context);
    void (*source_reset)(uint32_t epoch, void *context);
    void (*status_changed)(hog_client_state_t state, uint8_t status,
                           void *context);
    void *context;
} hog_client_callbacks_t;

void hog_client_init(const hog_client_callbacks_t *callbacks);
void hog_client_forget_peer(void);
hog_client_state_t hog_client_state(void);
uint32_t hog_client_epoch(void);

