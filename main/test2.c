#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include <sys/param.h>
// #include "esp_tls.h"
#include "esp_netif.h"
#include "cJSON.h"

// #include <sys/socket.h>
#include "esp_http_client.h"

#define WIFI_SSID "GEMTEA"
#define WIFI_PASSWORD "gemtea1068"

#define DEVICE_ID "124"
#define DEVICE_NAME "Device124"
#define DEVICE_TYPE 0
#define SERVER "http://192.168.0.115:8000/"
#define COLLECTION_PERIOD 1
#define SAMPLE_RATE 100
#define NUM_SAMPLES (COLLECTION_PERIOD * SAMPLE_RATE)
#define NUM_DECIMALS 4
// #define TIMEOUT 300000
// #define MAX_HTTP_OUTPUT_BUFFER 4096
#define LED_PIN GPIO_NUM_2

#define I2C_SCL_PIN 22
#define I2C_SDA_PIN 21

#define ADXL345_ADDRESS 0x53
#define ADXL345_DATA_FORMAT 0x31
#define ADXL345_POWER_CTL 0x2D
#define ADXL345_DATAX0 0x32
#define TIMEOUT 3000 // milliseconds
#define BUFFER_SIZE 1024
static const char *TAG = "HTTP_CLIENT";
// #define
static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        printf("WiFi connecting ... \n");
        break;
    case WIFI_EVENT_STA_CONNECTED:
        printf("WiFi connected ... \n");
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        printf("WiFi lost connection ... \n");
        break;
    case IP_EVENT_STA_GOT_IP:
        printf("WiFi got IP ... \n\n");
        break;
    default:
        break;
    }
}

void wifi_connection()
{
    // 1 - Wi-Fi/LwIP Init Phase
    esp_netif_init();                    // TCP/IP initiation 					s1.1
    esp_event_loop_create_default();     // event loop 			                s1.2
    esp_netif_create_default_wifi_sta(); // WiFi station 	                    s1.3
    wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_initiation); // 					                    s1.4
    // 2 - Wi-Fi Configuration Phase
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    wifi_config_t wifi_configuration = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD}};

    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);
    // 3 - Wi-Fi Start Phase
    esp_wifi_start();
    // 4- Wi-Fi Connect Phase
    esp_wifi_connect();
}

esp_err_t client_event_get_handler(esp_http_client_event_handle_t evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        printf("HTTP_EVENT_ON_DATA: %.*s\n", evt->data_len, (char *)evt->data);
        break;

    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t http_get_task()
{

    esp_http_client_config_t config_get = {
        .url = SERVER,
        .method = HTTP_METHOD_GET,
        .cert_pem = NULL,
        .event_handler = client_event_get_handler};

    esp_http_client_handle_t client = esp_http_client_init(&config_get);
    esp_http_client_perform(client);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %" PRId64,
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}
static esp_err_t i2c_master_init()
{
    int i2c_master_port = I2C_NUM_0;
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000};
    esp_err_t ret = i2c_param_config(i2c_master_port, &conf);
    if (ret != ESP_OK)
    {
        return ret;
    }
    return i2c_driver_install(i2c_master_port, conf.mode, 0, 0, 0);
}
static esp_err_t adxl345_init()
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, ADXL345_POWER_CTL, true);
    i2c_master_write_byte(cmd, 0x08, true); // Enable measurement mode
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}
static esp_err_t adxl345_read_xyz(float *x, float *y, float *z)
{
    uint8_t data[6];
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, ADXL345_DATAX0, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADXL345_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 6, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK)
    {
        *x = (int8_t)((data[1] << 8) | data[0]) / 256.0;
        *y = (int8_t)((data[3] << 8) | data[2]) / 256.0;
        *z = (int8_t)((data[5] << 8) | data[4]) / 256.0;
    }
    return ret;
}

static esp_err_t http_post_task(void *pvParameteres)
{

    // char *data_buf[250];
    cJSON *json_root = cJSON_CreateObject();
    if (json_root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON root object");
        return ESP_FAIL;
    }

    cJSON *json_name = cJSON_AddArrayToObject(json_root, "name");
    cJSON *json_type = cJSON_AddArrayToObject(json_root, "type");
    cJSON *json_id = cJSON_AddArrayToObject(json_root, "id");
    cJSON *json_x = cJSON_AddArrayToObject(json_root, "x");
    cJSON *json_y = cJSON_AddArrayToObject(json_root, "y");
    cJSON *json_z = cJSON_AddArrayToObject(json_root, "z");
    if (json_name == NULL || json_type == NULL || json_id == NULL || json_x == NULL || json_y == NULL || json_z == NULL)
    {
        ESP_LOGE(TAG, "Failed to add items to JSON object");
        cJSON_Delete(json_root); // Clean up allocated memory
        return ESP_FAIL;
    }

    adxl345_init();
    vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for sensor stabilization

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        float x, y, z;
        if (adxl345_read_xyz(&x, &y, &z) != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read ADXL345 data");
            cJSON_Delete(json_root); // Clean up allocated memory
            return ESP_FAIL;
        }

        cJSON_AddItemToArray(json_name, cJSON_CreateString(DEVICE_NAME));
        cJSON_AddItemToArray(json_type, cJSON_CreateNumber(DEVICE_TYPE));
        cJSON_AddItemToArray(json_id, cJSON_CreateString(DEVICE_ID));
        cJSON_AddItemToArray(json_x, cJSON_CreateNumber(x));
        cJSON_AddItemToArray(json_y, cJSON_CreateNumber(y));
        cJSON_AddItemToArray(json_z, cJSON_CreateNumber(z));
        vTaskDelay(pdMS_TO_TICKS(1000 / SAMPLE_RATE)); // Adjust delay for desired sample rate
    }
    char *strjson = cJSON_Print(json_root);
    ESP_LOGI(TAG, "%s", strjson);
    char *data_buf = (char *)malloc(sizeof(json_root) + 100); // Adjust size as needed
    cJSON_PrintPreallocated(json_root, data_buf, sizeof(data_buf), 1);
    cJSON_Delete(json_root);
    esp_http_client_config_t config = {
        .url = SERVER,
        .method = HTTP_METHOD_POST,
        .cert_pem = NULL,
        .event_handler = client_event_get_handler};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }
    char content_length_str[16];
    sprintf(content_length_str, "%d", (int)strlen(data_buf));
    ESP_LOGI(TAG, "%s", content_length_str);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Content-Length", content_length_str);
    esp_http_client_set_post_field(client, data_buf, (int)strlen(data_buf));
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %d", err);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
    free(*data_buf);
    return ESP_OK;
}

static void main_task(void *pvParameters)
{
    nvs_flash_init();

    wifi_connection();
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check server every 5 seconds

        // Send GET request to check if server is ready
        if (http_get_task() == ESP_OK)
        {

            // Server is ready, send POST request
            xTaskCreate(http_post_task, "http_post_task", 4096, NULL, 5, NULL);
        }
        else
        {
            ESP_LOGI(TAG, "Server is not ready");
        }
    }
}

void app_main()
{
    esp_rom_gpio_pad_select_gpio(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    i2c_master_init();
    // wifi_connection();
    // while (1)
    // {
    //     vTaskDelay(pdMS_TO_TICKS(5000)); // Check server every 5 seconds

    //     // Send GET request to check if server is ready
    //     if (http_get_task() == ESP_OK)
    //     {
    //         http_post_task();
    //         // Server is ready, send POST request
    //         // xTaskCreate(http_post_task, "http_post_task", 4096, NULL, 5, NULL);
    //     }
    //     else
    //     {
    //         ESP_LOGI(TAG, "Server is not ready");
    //     }
    // }

    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
}