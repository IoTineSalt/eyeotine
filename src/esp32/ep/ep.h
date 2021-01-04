#ifndef EYEOTINE_EP_H
#define EYEOTINE_EP_H

// Is this also available when building for ESP32?
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

// Possible log level:
//   0 - No log
//   1 - Errors
//   2 - Warnings
//   3 - Information
//   4 - Debug
#define EP_LOG_LEVEL 0

struct ep_header {
    uint8_t ver_type_subtype;
    uint8_t payload[0];
}; // todo Does the ESP compiler need __attribute__((packed)) here?

#define EP_GET_VERSION(packet) (((packet)->ver_type_subtype & 0xC0) >> 6)
#define EP_GET_TYPE(packet) (((packet)->ver_type_subtype & 0x30) >> 4)
#define EP_GET_SUBTYPE(packet) ((packet)->ver_type_subtype & 0x0F)

#define EP_SET_VERSION(packet, val) (packet)->ver_type_subtype = ((packet)->ver_type_subtype & 0x3F) | ((val & 0x03) << 6)
#define EP_SET_TYPE(packet, val) (packet)->ver_type_subtype = ((packet)->ver_type_subtype & 0xCF) | ((val & 0x03) << 4)
#define EP_SET_SUBTYPE(packet, val) (packet)->ver_type_subtype = ((packet)->ver_type_subtype & 0xF0) | (val & 0x0F)

#define EP_TYPE_MGMT 0
#define EP_SUBTYPE_MGMT_ASSOC 0
#define EP_SUBTYPE_MGMT_DASSOC 1
#define EP_SUBTYPE_MGMT_PING 2

#define EP_TYPE_CTRL 1
#define EP_SUBTYPE_CTRL_RESTART 0
#define EP_SUBTYPE_CTRL_SYNC 1
#define EP_SUBTYPE_CTRL_CONFIG 2

#define EP_TYPE_DATA 2
#define EP_SUBTYPE_DATA_IMAGE 0

struct ep_mgmt_assoc {
    struct ep_header header;
    uint8_t flags;
};

struct ep_mgmt_dassoc {
    struct ep_header header;
};

struct ep_mgmt_ping {
    struct ep_header header;
};

struct ep_ctrl_restart {
    struct ep_header header;
};

struct ep_ctrl_sync {
    struct ep_header header;
    uint8_t flags;
};

struct ep_ctrl_config {
    struct ep_header header;
    uint8_t config[0];
};

struct ep_data_image {
    struct ep_header header;
    uint16_t timestamp_fragno;
    uint8_t data[0];
};

#define EP_GET_DATA_TIMESTAMP(packet) (((packet)->timestamp_fragno & 0xFFC0) >> 6)
#define EP_GET_DATA_FRAGMENT_NO(packet) ((packet)->timestamp_fragno & 0x003F)

#define EP_SET_DATA_TIMESTAMP(packet, val) (packet)->timestamp_fragno = ((packet)->timestamp_fragno & 0x003F) | ((val & 0x03FF) << 6)
#define EP_SET_DATA_FRAGMENT_NO(packet, val) (packet)->timestamp_fragno = ((packet)->timestamp_fragno & 0xFFC0) | (val & 0x003F)

// API Functions

enum ep_state {
    STOPPED, IDLE, ASSOCIATING, WAIT_FOR_SYNC, CAMERA_OFF, CAMERA_ON
};

void ep_init(int (*send_udp)(void *, size_t), uint16_t (*milliclk)());
void ep_start();
void ep_stop();
void ep_recv_udp(void *buf, size_t len);
int ep_send_img(void *buf, size_t len);
void ep_set_connect_cb(void (*on_connect)());
void ep_set_disconnect_cb(void (*on_disconnect)());
void ep_set_recv_config_cb(void (*on_recv_config)(void *config, size_t len));
void ep_set_recv_restart_cb(void (*on_recv_restart)());



#endif //EYEOTINE_EP_H
