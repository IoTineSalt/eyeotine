#include "ep.h"

struct ep {
    enum ep_state state;
    int time_last_tx;
    int time_last_rx;
    void *buffer;
    int sync;

    // Callbacks
    void (*on_connect)();
    void (*on_disconnect)();
    void (*on_recv_config)(void *, size_t);
    void (*on_recv_restart)();
};

static struct ep ep;

void ep_init() {
    ep = (struct ep) {
        .state = STOPPED,
        .time_last_tx = -1, // todo
        .time_last_rx = -1, // todo
        .sync = -1,
        .buffer = (void *) 0xDEADBEEF // todo
    };
}

void ep_start() {
    if (ep.state == STOPPED) {
        ep.state = IDLE;
    }
}

void ep_stop() {
    // todo handle termination
    ep.state = STOPPED;
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

// Internal methods
static void recv(void *buf, size_t len);
static inline bool handle_disassociation(struct ep_header *eph);
static void perform_sync();

// Internal methods def
static void recv(void *buf, size_t len) {
    // Minimum length of the header is 1
    if (len < sizeof(struct ep_header)) {
        return;
    }

    struct ep_header *eph = (struct ep_header *) buf;
    // Only version 0 allowed
    if (EP_GET_VERSION(eph) != 0) {
        return;
    }

    // RX state machine
    if (ep.state == IDLE) {

        // Discard all packets
        return;

    } else if (ep.state == ASSOCIATING) {

        if (EP_GET_TYPE(eph) != EP_TYPE_MGMT || EP_GET_SUBTYPE(eph) != EP_SUBTYPE_MGMT_ASSOC) {
            // Discard all packets except for association frames
            return;
        }

        if (len < sizeof(struct ep_mgmt_assoc))
            return;


        struct ep_mgmt_assoc *assoc = (struct ep_mgmt_assoc *) buf;
        if ((assoc->flags & 1) == 0) {
            // Discard all requests
            return;
        }

        // Association successful. Update state
        ep.state = WAIT_FOR_SYNC;
        return;

    } else if (ep.state == WAIT_FOR_SYNC) {

        if (handle_disassociation(eph)) return;

        if (EP_GET_TYPE(eph) != EP_TYPE_CTRL || EP_GET_SUBTYPE(eph) != EP_SUBTYPE_CTRL_SYNC) {
            // Discard all packets except for association frames
            return;
        }

        if (len < sizeof(struct ep_ctrl_sync))
            return;

        struct ep_ctrl_sync *sync = (struct ep_ctrl_sync *) buf;
        perform_sync();
        ep.state = CAMERA_OFF;
        return;

    } else if (ep.state == CAMERA_OFF || ep.state == CAMERA_ON) {

        if (handle_disassociation(eph)) return;

        if (EP_GET_TYPE(eph) != EP_TYPE_CTRL && EP_GET_SUBTYPE(eph) != EP_SUBTYPE_CTRL_CONFIG) {
            if (len < sizeof(struct ep_ctrl_config))
                return;

            struct ep_ctrl_config *cfg = (struct ep_ctrl_config *) buf;
            if (ep.on_recv_config)
                ep.on_recv_config(cfg->config, len - sizeof(struct ep_ctrl_config));
            return;
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

}
