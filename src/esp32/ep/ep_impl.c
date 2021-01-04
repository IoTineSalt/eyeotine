#include "ep.h"

struct ep {
    enum ep_state state;
    int time_last_tx;
    int time_last_rx;
    void *buffer;
    int sync;

    // Interface
    int (*send_udp)(void *, size_t);
    uint16_t (*milliclk)();

    // Callbacks
    void (*on_connect)();
    void (*on_disconnect)();
    void (*on_recv_config)(void *, size_t);
    void (*on_recv_restart)();
};

// Internal declarations
static struct ep ep;
static bool recv(void *buf, size_t len);
static inline bool handle_disassociation(struct ep_header *eph);
static void perform_sync();
static int send_img_frag(void *buf, size_t len, uint8_t frag_no, uint16_t timestamp);

void ep_init(int (*send_udp)(void *buf, size_t len), uint16_t (*milliclk)()) {
    ep = (struct ep) {
        .state = STOPPED,
        .time_last_tx = -1, // todo
        .time_last_rx = -1, // todo
        .sync = -1,
        .buffer = (void *) 0xDEADBEEF, // todo malloc 2300
        .milliclk = milliclk,
        .send_udp = send_udp,
    };
}

void ep_start() {
    if (ep.state == STOPPED) {
        ep.state = IDLE;
    }
}

void ep_stop() {
    // todo handle termination, eg. send disconnect packet
    ep.state = STOPPED;
}

void ep_recv_udp(void *buf, size_t len) {
    if (recv(buf, len)) {
        ep.time_last_rx = ep.milliclk();
    }
}

int ep_send_img(void *buf, size_t len) {
    // todo error when state wrong
    // Careful, when decrementing len, since it is unsigned (!)
    size_t offset = 0;
    uint8_t frag_no = 0;
    uint16_t timestamp = (ep.milliclk() + 1024 - ep.sync) % 1024;
    int err;
    while (offset < len) {
        size_t frag_size = (len - offset > 2200) ? 2200 : len - offset;
        if ((err = send_img_frag(buf + offset, frag_size, frag_no, timestamp)) != 0) {
            return err;
        }
        frag_no++;
        offset += frag_size;
    }
    return 0;
}

void ep_set_connect_cb(void (*on_connect)()) {
    ep.on_connect = on_connect;
}

void ep_set_disconnect_cb(void (*on_disconnect)()) {
    ep.on_disconnect = on_disconnect;
}

void ep_set_recv_config_cb(void (*on_recv_config)(void *config, size_t len)) {
    ep.on_recv_config = on_recv_config;
}

void ep_set_recv_restart_cb(void (*on_recv_restart)()) {
    ep.on_recv_restart = on_recv_restart;
}

// Internal methods def
static bool recv(void *buf, size_t len) {
    // Minimum length of the header is 1
    if (len < sizeof(struct ep_header)) {
        return false;
    }

    struct ep_header *eph = (struct ep_header *) buf;
    // Only version 0 allowed
    if (EP_GET_VERSION(eph) != 0) {
        return false;
    }

    // RX state machine
    if (ep.state == IDLE) {
        // Discard all packets
        return false;
    } else if (ep.state == ASSOCIATING) {
        if (EP_GET_TYPE(eph) != EP_TYPE_MGMT || EP_GET_SUBTYPE(eph) != EP_SUBTYPE_MGMT_ASSOC) {
            // Discard all packets except for association frames
            return false;
        }

        if (len < sizeof(struct ep_mgmt_assoc)) {
            return false;
        }

        struct ep_mgmt_assoc *assoc = (struct ep_mgmt_assoc *) buf;
        if ((assoc->flags & 1) == 0) {
            // Discard all requests
            return false;
        }

        // Association successful. Update state
        if (ep.on_connect)
            ep.on_connect();
        ep.state = WAIT_FOR_SYNC;
        return true;
    } else if (ep.state == WAIT_FOR_SYNC || ep.state == CAMERA_OFF || ep.state == CAMERA_ON) {
        if (handle_disassociation(eph)) return true;

        if (EP_GET_TYPE(eph) != EP_TYPE_CTRL || EP_GET_TYPE(eph) != EP_TYPE_MGMT) {
            // Discard all packets except for mgmt and ctrl frames
            return false;
        }

        if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_SYNC) {
            if (len < sizeof(struct ep_ctrl_sync)) {
                return false;
            }

            perform_sync();
            if (ep.state == WAIT_FOR_SYNC)
                ep.state = CAMERA_OFF;
            return true;
        } else if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_RESTART) {
            if (len < sizeof(struct ep_ctrl_restart)) {
                return false;
            }

            if (ep.on_recv_restart)
                ep.on_recv_restart();
            return true;
        } else if (EP_GET_TYPE(eph) == EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_CTRL_CONFIG) {
            if (len < sizeof(struct ep_ctrl_config)) {
                return false;
            }

            struct ep_ctrl_config *cfg = (struct ep_ctrl_config *) buf;
            if (ep.on_recv_config)
                ep.on_recv_config(cfg->config, len - sizeof(struct ep_ctrl_config));
            return true;
        } else if (EP_GET_TYPE(eph) == EP_TYPE_MGMT && EP_GET_SUBTYPE(eph) == EP_SUBTYPE_MGMT_PING) {
            if (len < sizeof(struct ep_mgmt_ping))
                return false;

            // todo set the time
            return true;
        } else {
            // Unknown packet type
            return false;
        }
    }
}

static inline bool handle_disassociation(struct ep_header *eph) {
    if (EP_GET_TYPE(eph) != EP_TYPE_MGMT || EP_GET_SUBTYPE(eph) != EP_SUBTYPE_MGMT_DASSOC) {
        if (ep.on_disconnect)
            ep.on_disconnect();

        ep.state = IDLE;
        return true;
    }
    return false;
}

static void perform_sync() {
    ep.sync = ep.milliclk() % 1024;
}

static int send_img_frag(void *buf, size_t len, uint8_t frag_no, uint16_t timestamp) {
    struct ep_data_image *packet = (struct ep_data_image *) ep.buffer;
    struct ep_header *header = (struct ep_header *) packet;

    EP_SET_VERSION(header, 0);
    EP_SET_TYPE(header, EP_TYPE_DATA);
    EP_SET_SUBTYPE(header, EP_SUBTYPE_DATA_IMAGE);

    EP_SET_DATA_TIMESTAMP(packet, timestamp);
    EP_SET_DATA_FRAGMENT_NO(packet, frag_no);

    memcpy(&(packet->data), buf, len);

    return ep.send_udp(packet, sizeof(struct ep_data_image) + len);
}
