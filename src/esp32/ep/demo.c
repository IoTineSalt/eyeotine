#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include "ep.h"

int sock;
struct sockaddr_in sa_partner = {
    .sin_family = AF_INET
};
socklen_t sa_partner_len = sizeof(struct sockaddr_in);
bool camera_state = false;

uint16_t milliclk() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    uint16_t r = ts.tv_sec * 1e3;
    r += ts.tv_nsec / 1e6;
    return r;
}

int send_udp(void *buf, size_t len) {
    ssize_t n = sendto(sock, buf, len, 0, (const struct sockaddr *) &sa_partner, sizeof(struct sockaddr_in));
    if (n < 0) {
        perror("can't send data");
        return n;
    }

    if (n != len) {
        printf("send only %i of %u bytes", n, len);
    }
    return 0;
}

void logger(char *s) {
    puts(s);
}

void stop(int signum) {
    ep_stop();
    close(sock);
    exit(0);
}

void reassociate() {
    camera_state = false;
    ep_associate();
}

bool camera_switch(void *config, size_t len) {
    camera_state = len > 0;
    return camera_state;
}

int main(void) {

    struct sigaction s = {
        .sa_handler = stop,
        .sa_flags = 0
    };
    sigemptyset(&s.sa_mask);

    if (sigaction(SIGINT, &s, NULL) < 0) {
        perror("can't set interrupt handler");
        return 1;
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("can't create socket");
        close(sock);
        return 1;
    }

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = 0,
        .sin_addr.s_addr = htonl(INADDR_ANY)
    };

    if (bind(sock, (struct sockaddr *) &sa, sizeof(struct sockaddr_in)) < 0) {
        perror("can't bind socket");
        close(sock);
        return 1;
    }

    struct timeval tv = {
        .tv_sec = 0,
        .tv_usec = 10000
    };

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval)) < 0) {
        perror("can't set socket timeout");
        close(sock);
        return 1;
    }

    inet_aton("127.0.0.1", &(sa_partner.sin_addr));
    sa_partner.sin_port = htons(2021);

    ep_init(send_udp, milliclk, logger);
    // On disconnect instantly try to find a new connection
    ep_set_recv_config_cb(camera_switch);
    ep_set_disconnect_cb(reassociate);

    srand(0);

#define BUFLEN 4000
    void *buf = malloc(BUFLEN);
    if (!buf) {
        perror("can't allocate buffer");
        close(sock);
        return 1;
    }

    ep_start();
    ep_associate();

#define IMGBUFLEN 40000
    void *imgbuf = malloc(IMGBUFLEN);

    ssize_t n;
    while (1) {
        ep_loop();
        n = recvfrom(sock, buf, BUFLEN, 0, (struct sockaddr *) &sa_partner, &sa_partner_len);
        if (n < 0) {
            if (errno == EWOULDBLOCK) {
                // Nothing received
            } else {
                perror("can't receive from socket");
            }
        } else {
            ep_recv_udp(buf, n);
        }
        if (camera_state && rand() % 150 == 0) {
            int err;
            if ((err = ep_send_img(imgbuf, rand() % IMGBUFLEN)) != 0) {
                printf("Can't send image data. Error code: %i", err);
            }
        }
    }
}
