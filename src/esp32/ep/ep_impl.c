#include "ep.h"

#if EP_LOG_LEVEL > 0

#include <stdio.h>
#include <stdlib.h>

#endif

#if EP_LOG_LEVEL >= 3
#define EP_SET_STATE(s) {ep.state = s; ep.log("[INFO] current state: " #s);}
#else
#define EP_SET_STATE(s) ep.state = s
#endif

struct ep {
    enum ep_state state;
    uint16_t time_last_tx;
    uint16_t time_last_rx;
    void *buffer;
    int sync;
#if EP_LOG_LEVEL > 0
    char log_buffer[256];
#endif

    // Interface
    int (*send_udp)(void *, size_t);
    uint16_t (*milliclk)();
    void (*log)(char *msg);

    // Callbacks
    void (*on_connect)();
    void (*on_disconnect)();
    bool (*on_recv_config)(void *, size_t);
    void (*on_recv_restart)();
    void (*on_recv_ota)();
};

// Internal declarations
static struct ep ep;
static bool recv(void *buf, size_t len);
static inline bool handle_disassociation(struct ep_header *eph);
static void perform_sync();
static int send_img_frag(void *buf, size_t len, uint8_t frag_no, uint16_t timestamp, bool is_last_fragment);
static void log_none(char *str);

void ep_init(int (*send_udp)(void *buf, size_t len), uint16_t (*milliclk)(), void (*log)(char *msg)) {
    ep = (struct ep) {
        .state = STOPPED,
        .time_last_tx = milliclk() + 5000,
        .time_last_rx = milliclk() + 5000,
        .sync = -1,
        .buffer = malloc(2300),
        .milliclk = milliclk,
        .send_udp = send_udp,
        .log = log,
    };
#if EP_LOG_LEVEL > 0
    if (!ep.log) ep.log = log_none;
    if (ep.buffer == NULL) {
        ep.log("[ERR]  can't allocate ep buffer");
    }
#endif
#if EP_LOG_LEVEL >= 3
    ep.log("[INFO] ep initialized");
#endif
}

void ep_start() {
#if EP_LOG_LEVEL >= 4
    ep.log("[DBG]  ep_start() called");
#endif
    if (ep.state == STOPPED) {
        EP_SET_STATE(IDLE);
    }
}

void ep_stop() {
#if EP_LOG_LEVEL >= 4
    ep.log("[DBG]  ep_stop() called");
#endif
    struct ep_mgmt_dassoc *packet = (struct ep_mgmt_dassoc *) ep.buffer;
    struct ep_header *header = (struct ep_header *) packet;

    EP_SET_VERSION(header, 0);
    EP_SET_TYPE(header, EP_TYPE_MGMT);
    EP_SET_SUBTYPE(header, EP_SUBTYPE_MGMT_DASSOC);

    ep.send_udp(packet, sizeof(struct ep_mgmt_dassoc));
    ep.time_last_tx = ep.milliclk();

    if (ep.on_disconnect)
        ep.on_disconnect();

#if EP_LOG_LEVEL >= 3
    ep.log("[INFO] disassociated");
#endif

    EP_SET_STATE(STOPPED);
}

void ep_loop() {
#if EP_LOG_LEVEL >= 4
    ep.log("[DBG]  ep_loop() called");
#endif

    uint16_t time = ep.milliclk();
    if (ep.state == ASSOCIATING) {
        // Send association requests every 5 seconds
        if ((uint16_t) (time - ep.time_last_tx) >= 5000) {
            struct ep_mgmt_assoc *packet = (struct ep_mgmt_assoc *) ep.buffer;
            struct ep_header *header = (struct ep_header *) packet;

            EP_SET_VERSION(header, 0);
            EP_SET_TYPE(header, EP_TYPE_MGMT);
            EP_SET_SUBTYPE(header, EP_SUBTYPE_MGMT_ASSOC);

            packet->flags = 0;

            ep.send_udp(packet, sizeof(struct ep_mgmt_assoc));
            ep.time_last_tx = time;
#if EP_LOG_LEVEL >= 3
            ep.log("[INFO] sending association request");
#endif
        }
    } else if (ep.state == WAIT_FOR_SYNC || ep.state == CAMERA_ON || ep.state == CAMERA_OFF) {
        // Send ping every second
        if ((uint16_t) (time - ep.time_last_tx) >= 1000) {
            struct ep_mgmt_ping *packet = (struct ep_mgmt_ping *) ep.buffer;
            struct ep_header *header = (struct ep_header *) packet;

            EP_SET_VERSION(header, 0);
            EP_SET_TYPE(header, EP_TYPE_MGMT);
            EP_SET_SUBTYPE(header, EP_SUBTYPE_MGMT_PING);

            ep.send_udp(packet, sizeof(struct ep_mgmt_ping));
            ep.time_last_tx = time;
#if EP_LOG_LEVEL >= 3
            ep.log("[INFO] sending ping");
#endif
        }

        // Check for timeout
        if ((uint16_t) (time - ep.time_last_rx) >= 5000) {
#if EP_LOG_LEVEL >= 2
            ep.log("[WARN] connection timeout");
#endif
            EP_SET_STATE(IDLE);
            if (ep.on_disconnect)
                ep.on_disconnect();

        }
    }
}

void ep_associate() {
#if EP_LOG_LEVEL >= 4
    ep.log("[DBG]  ep_associate() called");
#endif
    if (ep.state == IDLE) {
        EP_SET_STATE(ASSOCIATING);
    }
}

void ep_recv_udp(void *buf, size_t len) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_recv_udp(%p, %u) called", buf, len);
    ep.log(ep.log_buffer);
#endif
    if (recv(buf, len)) {
        ep.time_last_rx = ep.milliclk();
    }
}

int ep_send_img(void *buf, size_t len) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_send_image(%p, %u) called", buf, len);
    ep.log(ep.log_buffer);
#endif
    if (ep.state != CAMERA_ON) {
#if EP_LOG_LEVEL >= 2
        ep.log("[WARN] can't send image in current state");
#endif
        return -1;
    }
#if EP_LOG_LEVEL >= 3
    sprintf(ep.log_buffer, "[INFO] new image to send (%u bytes)", len);
    ep.log(ep.log_buffer);
#endif

    size_t offset = 0;
    uint8_t frag_no = 0;
    uint16_t timestamp = (ep.milliclk() + 1024 - ep.sync) % 1024;
    int err;
    while (offset < len) {
        size_t frag_size = (len - offset > 2200) ? 2200 : len - offset;
        if ((err = send_img_frag(buf + offset, frag_size, frag_no, timestamp, len - offset <= 2200)) != 0) {
            return err;
        }
        frag_no++;
        offset += frag_size;
    }
#if EP_LOG_LEVEL >= 3
    ep.log("[INFO] image send successfully");
#endif
    return 0;
}

void ep_set_connect_cb(void (*on_connect)()) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_set_connect_cb(%p) called", on_connect);
    ep.log(ep.log_buffer);
#endif
    ep.on_connect = on_connect;
}

void ep_set_disconnect_cb(void (*on_disconnect)()) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_set_disconnect_cb(%p) called", on_disconnect);
    ep.log(ep.log_buffer);
#endif
    ep.on_disconnect = on_disconnect;
}

void ep_set_recv_config_cb(bool (*on_recv_config)(void *config, size_t len)) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_set_recv_config_cb(%p) called", on_recv_config);
    ep.log(ep.log_buffer);
#endif
    ep.on_recv_config = on_recv_config;
}

void ep_set_recv_restart_cb(void (*on_recv_restart)()) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_recv_restart_cb(%p) called", on_recv_restart);
    ep.log(ep.log_buffer);
#endif
    ep.on_recv_restart = on_recv_restart;
}

void ep_set_ota_cb(void (*on_recv_ota)()) {
#if EP_LOG_LEVEL >= 4
    sprintf(ep.log_buffer, "[DBG]  ep_recv_restart_cb(%p) called", on_recv_restart);
    ep.log(ep.log_buffer);
#endif
    ep.on_recv_ota = on_recv_ota;
}

// Internal method definitions
static bool recv(void *buf, size_t len) {
    // Minimum length of the header is 1
    if (len < sizeof(struct ep_header)) {
#if EP_LOG_LEVEL >= 2
        sprintf(ep.log_buffer, "[WARN] truncated header (expected %u bytes, got %u)", sizeof(struct ep_header), len);
        ep.log(ep.log_buffer);
#endif
        return false;
    }

#if EP_LOG_LEVEL >= 2
#define EP_WARN_MISMATCH(msg, fmt_exp, exp, fmt_got, got) {\
            sprintf(ep.log_buffer, "[WARN] " msg " (expected " fmt_exp ", got " fmt_got ")", exp, got); \
            ep.log(ep.log_buffer);}
#else
#define EP_WARN_MISMATCH(msg, fmt_exp, exp, fmt_got, got)
#endif

    struct ep_header *eph = (struct ep_header *) buf;
    // Only version 0 allowed
    if (EP_GET_VERSION(eph) != 0) {
        EP_WARN_MISMATCH("invalid ep version", "%i", 0, "%i", EP_GET_VERSION(eph));
        return false;
    }

#if EP_LOG_LEVEL >= 3
#define EP_INFO_DISCARD(state) sprintf(ep.log_buffer, "[INFO] discarding packet in state " state " (type %i, subtype %i)", \
                                EP_GET_TYPE(eph), EP_GET_SUBTYPE(eph)); \
                                ep.log(ep.log_buffer);
#else
#define EP_INFO_DISCARD(state)
#endif

    // RX state machine
    if (ep.state == IDLE) {
        // Discard all packets
        EP_INFO_DISCARD("idle");
        return false;
    } else if (ep.state == ASSOCIATING) {
        if (EP_GET_TYPE(eph) != EP_TYPE_MGMT || EP_GET_SUBTYPE(eph) != EP_SUBTYPE_MGMT_ASSOC) {
            // Discard all packets except for association frames
            EP_INFO_DISCARD("associating");
            return false;
        }

        if (len < sizeof(struct ep_mgmt_assoc)) {
            EP_WARN_MISMATCH("truncated association frame", "%u", sizeof(struct ep_mgmt_assoc), "%u", len);
            return false;
        }

        struct ep_mgmt_assoc *assoc = (struct ep_mgmt_assoc *) buf;
        if ((assoc->flags & 1) == 0) {
            // Discard all requests
            EP_WARN_MISMATCH("unexpected flags in association", "%x", 1, "%u", assoc->flags);
            return false;
        }

        // Association successful. Update state
        if (ep.on_connect)
            ep.on_connect();
        EP_SET_STATE(WAIT_FOR_SYNC);
#if EP_LOG_LEVEL >= 3
        ep.log("[INFO] received association frame");
#endif
        return true;
    } else if (ep.state == WAIT_FOR_SYNC || ep.state == CAMERA_OFF || ep.state == CAMERA_ON) {
        if (handle_disassociation(eph)) return true;

        if (EP_GET_TYPE(eph) != EP_TYPE_CTRL && EP_GET_TYPE(eph) != EP_TYPE_MGMT) {
            // Discard all packets except for mgmt and ctrl frames
            EP_INFO_DISCARD("wait for sync/camera {on,off}");
            return false;
        }

        if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_OTA) {
            if (len < sizeof(struct ep_ctrl_ota)) {
                EP_WARN_MISMATCH("truncated ota frame", "%u", sizeof(struct ep_ctrl_ota), "%u", len);
                return false;
            }
#if EP_LOG_LEVEL >= 3
            ep.log("[INFO] received ota");

            if (ep.on_recv_ota)
                ep.on_recv_ota();

            return true;
#endif
        }

        if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_SYNC) {
            if (len < sizeof(struct ep_ctrl_sync)) {
                EP_WARN_MISMATCH("truncated sync frame", "%u", sizeof(struct ep_ctrl_sync), "%u", len);
                return false;
            }

            perform_sync();
            if (ep.state == WAIT_FOR_SYNC) EP_SET_STATE(CAMERA_OFF);
#if EP_LOG_LEVEL >= 3
            ep.log("[INFO] received sync");
#endif
            return true;
        } else if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_RESTART) {
            if (len < sizeof(struct ep_ctrl_restart)) {
                EP_WARN_MISMATCH("truncated restart frame", "%u", sizeof(struct ep_ctrl_restart), "%u", len);
                return false;
            }

            if (ep.on_recv_restart)
                ep.on_recv_restart();
#if EP_LOG_LEVEL >= 3
            ep.log("[INFO] received restart");
#endif
            return true;
        } else if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_CONFIG) {
            if (len < sizeof(struct ep_ctrl_config)) {
                EP_WARN_MISMATCH("truncated config frame", "%u", sizeof(struct ep_ctrl_config), "%u", len);
                return false;
            }

            struct ep_ctrl_config *cfg = (struct ep_ctrl_config *) buf;
            if (ep.on_recv_config) {
                if (ep.on_recv_config(cfg->config, len - sizeof(struct ep_ctrl_config))) {
                    EP_SET_STATE(CAMERA_ON);
                } else {
                    EP_SET_STATE(CAMERA_OFF);
                }
            }
#if EP_LOG_LEVEL >= 3
            sprintf(ep.log_buffer, "[INFO] received config (%u bytes)", len - sizeof(struct ep_ctrl_config));
            ep.log(ep.log_buffer);
#endif
            return true;
        } else if (EP_GET_TYPE(eph) == EP_TYPE_MGMT && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_MGMT_PING) {
            if (len < sizeof(struct ep_mgmt_ping)) {
                EP_WARN_MISMATCH("truncated ping frame", "%u", sizeof(struct ep_mgmt_ping), "%u", len);
                return false;
            }

#if EP_LOG_LEVEL >= 3
            ep.log("[INFO] received ping");
#endif
            // last_time_rx will be set in calling function if we return true
            return true;
        } else {
            // Unknown packet type
#if EP_LOG_LEVEL >= 2
            sprintf(ep.log_buffer, "[WARN] unknown type/subtype combination (type: %x, subtype: %x)", EP_GET_TYPE(eph), EP_GET_SUBTYPE(eph));
            ep.log(ep.log_buffer);
#endif
            return false;
        }
    }
#if EP_LOG_LEVEL >= 1
    sprintf(ep.log_buffer, "[ERR]  incoming packet in unknown state: %i", ep.state);
    ep.log(ep.log_buffer);
#endif
    return false;
}

#undef EP_WARN_MISMATCH
#undef EP_INFO_DISCARD

static inline bool handle_disassociation(struct ep_header *eph) {
    if (EP_GET_TYPE(eph) == EP_TYPE_MGMT && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_MGMT_DASSOC) {
        if (ep.on_disconnect)
            ep.on_disconnect();

#if EP_LOG_LEVEL >= 3
        ep.log("[INFO] successful disassociation");
#endif
        EP_SET_STATE(IDLE);
        return true;
    }
    return false;
}

static void perform_sync() {
    ep.sync = ep.milliclk() % 1024;
}

static int send_img_frag(void *buf, size_t len, uint8_t frag_no, uint16_t timestamp, bool is_last_fragment) {
    struct ep_data_image *packet = (struct ep_data_image *) ep.buffer;
    struct ep_header *header = (struct ep_header *) packet;

    EP_SET_VERSION(header, 0);
    EP_SET_TYPE(header, EP_TYPE_DATA);
    EP_SET_SUBTYPE(header, EP_SUBTYPE_DATA_IMAGE);

    packet->flags = is_last_fragment ? 1 : 0;

    EP_SET_DATA_TIMESTAMP(packet, timestamp);
    EP_SET_DATA_FRAGMENT_NO(packet, frag_no);

    memcpy(&(packet->data), buf, len);

    return ep.send_udp(packet, sizeof(struct ep_data_image) + len);
}

static void log_none(char *str) {}

#undef EP_SET_STATE
