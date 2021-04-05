#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "string.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_spi_flash.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "../ep/ep.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

#include "esp_wifi.h"

static const char *TAGOTA = "OTA";
static const char *TAG = "eyeotine";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

static bool send_images = false;
static int udpsock;

#define OTA_URL_SIZE 256

esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAGOTA, "HTTP_EVENT_DISCONNECTED");
            break;
    }
    return ESP_OK;
}

void ota_task(void *pvParameter) {
    ESP_LOGI(TAGOTA, "Starting OTA");

    esp_http_client_config_t config = {
        .url = "http://192.168.0.231:8080/",
        .cert_pem = (char *) server_cert_pem_start,
        .event_handler = _http_event_handler,
    };

    // todo make this secure
    config.skip_cert_common_name_check = true;

    esp_err_t ret = esp_https_ota(&config);
    if (ret == ESP_OK) {
        esp_restart();
    } else {
        ESP_LOGW(TAGOTA, "Firmware upgrade not available");
    }
    while (1) {
        // todo remove this? maybe vTaskDelete is the right function
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

static struct sockaddr_in sin = {
    .sin_family = AF_INET,
    .sin_port = PP_HTONS(CONFIG_EYEOTINE_SERVER_PORT),
};

int eyeotine_send_udp(void *buf, size_t len) {
    // todo retry a few times if ERRWOULDBLOCK
    int err = sendto(udpsock, buf, len, 0, (struct sockaddr *) &sin, sizeof(sin));
    if (err > 0) err = 0;
    return err;
}

uint16_t eyeotine_milliclk() {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    uint16_t r = tv.tv_sec * 1e3;
    r += tv.tv_usec / 1e3;
    return r;
}

void eyeotine_log(char *msg) {
    ESP_LOGI(TAG, "EP says %s", msg);
}

void eyeotine_on_disconnect() {
    ESP_LOGI(TAG, "Disconnected");
    ep_associate();
}

bool eyeotine_on_config(void *config, size_t len) {
    send_images = !send_images;

    ESP_LOGI(TAG, "Camera config set to: %i", send_images);
    // todo implement config spec
    return send_images;
}

void start_ota() {
    ESP_LOGI(TAG, "Received OTA");
    ESP_LOGI(TAG, "Shutting down EP");
    ep_stop();

    // Start OTA Task
    xTaskCreate(&ota_task, "ota_task", 8192, NULL, 5, NULL);
}

void eyeotine_camera_task(void *pvParameter);

void eyeotine_task(void *pvParameter) {
    ESP_LOGI(TAG, "Starting operation");

    // Setup UDP socket
    udpsock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udpsock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        goto ep_error;
    }

    int flags = fcntl(udpsock, F_GETFL);
    fcntl(udpsock, F_SETFL, flags | O_NONBLOCK);

    sin.sin_addr.s_addr = inet_addr(CONFIG_EYEOTINE_SERVER_IP);

    ESP_LOGI(TAG, "Created UDP socket");

    ep_init(eyeotine_send_udp, eyeotine_milliclk, eyeotine_log);
    ep_set_disconnect_cb(eyeotine_on_disconnect);
    ep_set_recv_config_cb(eyeotine_on_config);
    ep_set_ota_cb(start_ota);

#define BUFLEN 2500
    void *buf = malloc(BUFLEN);
    if (!buf) {
        ESP_LOGE(TAG, "Can't allocate receive buffer");
    }

    ep_start();
    ep_associate();

    xTaskCreate(&eyeotine_camera_task, "eyeotine_camera_task", 8192, NULL, 4, NULL);

    while (1) {
        ep_loop();

        struct sockaddr_in src;
        socklen_t srclen = sizeof(src);
        int len = recvfrom(udpsock, buf, BUFLEN, 0, (struct sockaddr *) &src, &srclen);
        if (len < 0) {
            if (errno == EWOULDBLOCK) {
                vTaskDelay(30 / portTICK_PERIOD_MS);
            } else {
                ESP_LOGE(TAG, "Can't receive UDP. Errno %d", errno);
            }
        } else {
            ep_recv_udp(buf, len);
        }
    }
#undef BUFLEN

    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "EP Success");
    }

    ep_error:
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "EP Error ++ EP Error ++ EP Error ++ EP Error ++ EP Error");
    }
    return;
}

//camera_config_t camera_config = {
//    .ledc_channel = LEDC_CHANNEL_0,
//    .ledc_timer = LEDC_TIMER_0,
//    .pin_d0 = 35,
//    .pin_d1 = 17,
//    .pin_d2 = 34,
//    .pin_d3 = 5,
//    .pin_d4 = 39,
//    .pin_d5 = 18,
//    .pin_d6 = 36,
//    .pin_d7 = 19,
//    .pin_xclk = 27,
//    .pin_pclk = 21,
//    .pin_vsync = 22,
//    .pin_href = 26,
//    .pin_sscb_sda = 25,
//    .pin_sscb_scl = 23,
//    .pin_reset = 15,
//    .xclk_freq_hz = 20000000,
//};
//
//#define CAMERA_PIXEL_FORMAT CAMERA_PF_GRAYSCALE
//#define CAMERA_FRAME_SIZE CAMERA_FS_QVGA
//
//static camera_pixelformat_t s_pixel_format;
//
//void eyeotine_camera_task(void *pvParameter) {
//    camera_model_t camera_model;
//    esp_err_t err = camera_probe(&camera_config, &camera_model);
//    if (err != ESP_OK) {
//        ESP_LOGE(TAG, "Camera probe failed with error 0x%x", err);
//        goto cam_error;
//    }
//
//    if (camera_model == CAMERA_OV7725) {
//        s_pixel_format = CAMERA_PIXEL_FORMAT;
//        camera_config.frame_size = CAMERA_FRAME_SIZE;
//        ESP_LOGI(TAG, "Detected OV7725 camera, using %s bitmap format",
//                 CAMERA_PIXEL_FORMAT == CAMERA_PF_GRAYSCALE ?
//                 "grayscale" : "RGB565");
//    } else if (camera_model == CAMERA_OV2640) {
//        ESP_LOGI(TAG, "Detected OV2640 camera, using JPEG format");
//        s_pixel_format = CAMERA_PF_JPEG;
//        camera_config.frame_size = CAMERA_FRAME_SIZE;
//        camera_config.jpeg_quality = 15;
//    } else {
//        ESP_LOGE(TAG, "Camera not supported");
//        goto cam_error;
//    }
//
//
//    camera_config.pixel_format = s_pixel_format;
//    err = camera_init(&camera_config);
//    if (err != ESP_OK) {
//        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
//        goto cam_error;
//    }
//
//    err = camera_run();
//    if (err != ESP_OK) {
//        ESP_LOGD(TAG, "Camera capture failed with error 0x%x", err);
//        goto cam_error;
//    }
//
//    while (1) {
//        if (send_images) {
//            ESP_LOGI(TAG, "Trying to send image...");
//            ep_send_img(camera_get_fb(), camera_get_data_size());
//        }
//        // todo we will need to consider, that sending of the image may take quite a few milliseconds
//        vTaskDelay(1000 / portTICK_PERIOD_MS);
//    }
//
//    cam_error:
//    while (1) {
//        ESP_LOGI(TAG, "Cam Error: 0x%x", err);
//        vTaskDelay(1000 / portTICK_PERIOD_MS);
//    }
//
//}

//ESP32-CAM
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1 //software reset will be performed
#define CAM_PIN_XCLK    0//GPIO
#define CAM_PIN_SIOD    26//GPIO
#define CAM_PIN_SIOC    27//GPIO

#define CAM_PIN_D7      35//GPIO
#define CAM_PIN_D6      34//GPIO
#define CAM_PIN_D5      39//GPIO (sensor VN)
#define CAM_PIN_D4      36//GPIO (sensor VP)
#define CAM_PIN_D3      21//GPIO
#define CAM_PIN_D2      19//GPIO
#define CAM_PIN_D1      18//GPIO
#define CAM_PIN_D0       5//GPIO
#define CAM_PIN_VSYNC   25//GPIO
#define CAM_PIN_HREF    23//GPIO
#define CAM_PIN_PCLK    22//GPIO

static camera_config_t camera_config = {
    .pin_pwdn  = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sscb_sda = CAM_PIN_SIOD,
    .pin_sscb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    //XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 10000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,//YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_UXGA,//QQVGA-QXGA Do not use sizes above QVGA when not JPEG

    .jpeg_quality = 12, //0-63 lower number means higher quality
    .fb_count = 1 //if more than one, i2s runs in continuous mode. Use only with JPEG
};

void eyeotine_camera_task(void *pvParameter) {
    //power up the camera if PWDN pin is defined
    if (CAM_PIN_PWDN != -1) {
        gpio_pad_select_gpio(CAM_PIN_PWDN);
        gpio_set_direction(CAM_PIN_PWDN, GPIO_MODE_OUTPUT);
        gpio_set_level(CAM_PIN_PWDN, 0);
    }

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        goto cam_error;
    }

    while (1) {
        if (send_images) {
            ESP_LOGI(TAG, "Trying to send image...");
            camera_fb_t *fb = esp_camera_fb_get();
            int e = ep_send_img(fb->buf, fb->len);
            if (e != 0) {
                ESP_LOGW(TAG, "Sending image failed with error: %i", e);
            }
            esp_camera_fb_return(fb);
        }
        // todo we will need to consider, that sending of the image may take quite a few milliseconds
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

    cam_error:
    while (1) {
        ESP_LOGI(TAG, "Cam Error: 0x%x", err);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

}

// The next methods are copied from esp-idf's protocol_examples_common.
// Maybe we can improve them since they might be designed for a broader use case than ours.

static xSemaphoreHandle s_semph_get_ip_addr;
static esp_ip4_addr_t s_ip_addr;
static esp_netif_t *s_example_esp_netif;

/**
 * @brief Checks the netif description if it contains specified prefix.
 * All netifs created withing common connect component are prefixed with the module TAG,
 * so it returns true if the specified netif is owned by this module
 */
static bool is_our_netif(const char *prefix, esp_netif_t *netif) {
    return strncmp(prefix, esp_netif_get_desc(netif), strlen(prefix) - 1) == 0;
}

static void on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
    if (!is_our_netif(TAG, event->esp_netif)) {
        ESP_LOGW(TAG, "Got IPv4 from another interface \"%s\": ignored", esp_netif_get_desc(event->esp_netif));
        return;
    }
    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: "
    IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    memcpy(&s_ip_addr, &event->ip_info.ip, sizeof(s_ip_addr));
    xSemaphoreGive(s_semph_get_ip_addr);
}

static esp_netif_t *wifi_start(void) {
    char *desc;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Prefix the interface description with the module TAG
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    asprintf(&desc, "%s: %s", TAG, esp_netif_config.if_desc);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio = 128;
    esp_netif_t *netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    free(desc);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_EYEOTINE_WIFI_SSID,
            .password = CONFIG_EYEOTINE_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    return netif;
}

esp_netif_t *get_example_netif_from_desc(const char *desc) {
    esp_netif_t *netif = NULL;
    char *expected_desc;
    asprintf(&expected_desc, "%s: %s", TAG, desc);
    while ((netif = esp_netif_next(netif)) != NULL) {
        if (strcmp(esp_netif_get_desc(netif), expected_desc) == 0) {
            free(expected_desc);
            return netif;
        }
    }
    free(expected_desc);
    return netif;
}

static void wifi_stop(void) {
    esp_netif_t *wifi_netif = get_example_netif_from_desc("sta");
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip));
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(wifi_netif));
    esp_netif_destroy(wifi_netif);
    s_example_esp_netif = NULL;
}

esp_err_t wifi_connect(void) {
    if (s_semph_get_ip_addr != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_example_esp_netif = wifi_start();
    s_semph_get_ip_addr = xSemaphoreCreateCounting(1, 0);
    ESP_ERROR_CHECK(esp_register_shutdown_handler(&wifi_stop));
    ESP_LOGI(TAG, "Waiting for IP(s)");
    xSemaphoreTake(s_semph_get_ip_addr, portMAX_DELAY);
    // iterate over active interfaces, and print out IPs of "our" netifs
    esp_netif_t *netif = NULL;
    esp_netif_ip_info_t ip;
    for (int i = 0; i < esp_netif_get_nr_of_ifs(); ++i) {
        netif = esp_netif_next(netif);
        if (is_our_netif(TAG, netif)) {
            ESP_LOGI(TAG, "Connected to %s", esp_netif_get_desc(netif));
            ESP_ERROR_CHECK(esp_netif_get_ip_info(netif, &ip));

            ESP_LOGI(TAG, "- IPv4 address: "
            IPSTR, IP2STR(&ip.ip));
        }
    }
    return ESP_OK;
}

void app_main(void) {
    // Initialize NVS.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // 1.OTA app partition table has a smaller NVS partition size than the non-OTA
        // partition table. This size mismatch may cause NVS initialization to fail.
        // 2.NVS partition contains data in new format and cannot be recognized by this version of code.
        // If this happens, we erase NVS partition and initialize NVS again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(wifi_connect());
    esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "WiFi connection successful");

    xTaskCreate(&eyeotine_task, "eyeotine_task", 8192, NULL, 5, NULL);
}
