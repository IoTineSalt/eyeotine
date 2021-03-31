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
#ifndef EP_LOG_LEVEL
#define EP_LOG_LEVEL 3
#endif

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
    uint8_t flags;
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

/*************************************************************************************//**
 * Initialize the EP library.
 *
 * This function must be called prior to any other function calls.
 * You can also call it, in case you want to reset the EP instance.
 *
 * @param send_udp A function that sends len bytes from buf via UDP to the EP server.
 *                 It shall return 0 on success and a non-zero error code otherwise.
 * @param milliclk A function that returns the current time in milliseconds. It doesn't
 *                 need to be calibrated to a fixed reference time, but should not change
 *                 the reference time at any point. The return type will require it to
 *                 regularly overflow (every 65.536 seconds)
 * @param log      A function that print its null-terminated argument to a logger. You
 *                 can also set it to NULL to disable logging.
 ****************************************************************************************/
void ep_init(int (*send_udp)(void *buf, size_t len), uint16_t (*milliclk)(), void (*log)(char *msg));

/********************************************************************//**
 * Start the EP client
 *
 * The EP client will be set into IDLE state, if it is in STOPPED state
 ***********************************************************************/
void ep_start();

/***********************************************************************//**
 * Stop the EP client
 *
 * The EP client will be set forced into the STOPPED state. If a connection
 * is active, the EP server will be notified of the disconnect.
 **************************************************************************/
void ep_stop();

/***********************************************************************//**
 * Poll client jobs
 *
 * The EP client has several time-based jobs that need to be executed in
 * approximate intervals. A good interval to call this function would be
 * between every 10 to 100 ms
 **************************************************************************/
void ep_loop();

/***********************************************************************//**
 * Try to associate to the server
 *
 * This function sets the EP instance to ASSOCIATING mode if is currently
 * IDLE. It needs to be called after ep_start() or after a connection has
 * been terminated to reconnect to the server.
 *
 * In ASSOCIATING mode, the client will send an association request every
 * 5 seconds until it is stopped or gets an association response.
 **************************************************************************/
void ep_associate();

/***********************************************************************//**
 * Receive an EP packet
 *
 * If an EP packet is received on the corresponding UDP socket, the program
 * shall call this function to let the library process it.
 *
 * @param buf Buffer containing the UDP payload
 * @param len Length of the buffer
 **************************************************************************/
void ep_recv_udp(void *buf, size_t len);

/********************************************************************//**
 * Send an image to the EP server
 *
 * This functions sends the whole buffer in correctly sized fragments to
 * the server.
 *
 * @param buf Buffer containing the image data
 * @param len Length of the buffer
 * @return 0 for success, otherwise a non-zero error code
 ***********************************************************************/
int ep_send_img(void *buf, size_t len);

/************************************************//**
 * Set a callback for a associating with the server
 *
 * @param on_connect Callback handler
 ***************************************************/
void ep_set_connect_cb(void (*on_connect)());

/**************************************************************************//**
 * Set a callback for the event, when either partner terminates the connection
 *
 * @param on_disconnect Callback handler
 *****************************************************************************/
void ep_set_disconnect_cb(void (*on_disconnect)());

/*******************************************************************************//**
 * Set a callback for the event, when a config frame is received
 *
 * @param on_recv_config Callback handler. Receives a buffer with the configuration
 *                       and its length as parameters. Returns true, if the camera
 *                       is activated, else false.
 **********************************************************************************/
void ep_set_recv_config_cb(bool (*on_recv_config)(void *config, size_t len));

/**************************************************************************//**
 * Set a callback for an incoming restart frame
 *
 * @param on_recv_restart Callback handler
 *****************************************************************************/
void ep_set_recv_restart_cb(void (*on_recv_restart)());


#endif //EYEOTINE_EP_H
