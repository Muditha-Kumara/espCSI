/*
 * ESP-NOW Protocol for CSI Data Collection System
 * Shared between receiver and broadcaster
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ESPNOW_PROTO_MAGIC 0x43534950  // "CSIP"

typedef enum {
    ESPNOW_CMD_ANNOUNCE = 0x01,    // Broadcaster announces itself
    ESPNOW_CMD_START = 0x02,       // Receiver sends start command
    ESPNOW_CMD_STOP = 0x03,        // Receiver sends stop command
    ESPNOW_CMD_CONFIG = 0x04,      // Receiver sends config (OTA URL, etc)
    ESPNOW_CMD_ACK = 0x05,         // Acknowledgment
} espnow_cmd_t;

// Broadcaster announce message (sent every 5s when idle)
typedef struct __attribute__((packed)) {
    uint32_t magic;                // ESPNOW_PROTO_MAGIC
    uint8_t cmd;                   // ESPNOW_CMD_ANNOUNCE
    uint8_t mac[6];                // Broadcaster MAC
    char version[16];              // Firmware version (null-terminated)
    uint8_t channel;               // Current channel
    uint8_t reserved[2];
} espnow_announce_t;

// Start session command (receiver -> broadcaster)
typedef struct __attribute__((packed)) {
    uint32_t magic;                // ESPNOW_PROTO_MAGIC
    uint8_t cmd;                   // ESPNOW_CMD_START
    uint8_t session_id[16];        // Session identifier (timestamp-based)
    uint8_t channel;               // Work channel (1-13)
    uint16_t rate_hz;              // Broadcast rate (10-200 Hz)
    uint32_t duration_s;           // Session duration in seconds
    uint8_t reserved[4];
} espnow_start_t;

// Stop session command (receiver -> broadcaster)
typedef struct __attribute__((packed)) {
    uint32_t magic;                // ESPNOW_PROTO_MAGIC
    uint8_t cmd;                   // ESPNOW_CMD_STOP
    uint8_t session_id[16];        // Session to stop
    uint8_t reserved[7];
} espnow_stop_t;

// Config command (receiver -> broadcaster)
typedef struct __attribute__((packed)) {
    uint32_t magic;                // ESPNOW_PROTO_MAGIC
    uint8_t cmd;                   // ESPNOW_CMD_CONFIG
    char ota_url[64];              // OTA server URL (null-terminated)
    uint8_t reserved[3];
} espnow_config_t;

// Acknowledgment (broadcaster -> receiver)
typedef struct __attribute__((packed)) {
    uint32_t magic;                // ESPNOW_PROTO_MAGIC
    uint8_t cmd;                   // ESPNOW_CMD_ACK
    uint8_t ack_cmd;               // Command being acknowledged
    uint8_t status;                // 0 = success, 1 = error
    uint8_t reserved[5];
} espnow_ack_t;

// Union of all message types
typedef union {
    espnow_announce_t announce;
    espnow_start_t start;
    espnow_stop_t stop;
    espnow_config_t config;
    espnow_ack_t ack;
} espnow_msg_t;

#ifdef __cplusplus
}
#endif
