#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/twai.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "RECEIVER";

#define CAN_TX_PIN 26
#define CAN_RX_PIN 25

#define SPI_MISO_PIN 19
#define SPI_MOSI_PIN 23
#define SPI_CLK_PIN  18
#define SPI_CS_PIN   5


typedef union {
    float f_val;
    uint32_t u_val;
    uint8_t bytes[4];
} payload_t;

typedef struct {
    uint32_t timestamp;
    uint32_t packet_id;
    float chamber_pressure;
    float tank_pressure;
    float accel_z;
    float altitude;
} telemetry_t;


QueueHandle_t telemetry_queue;

char current_filename[64];

//CAN Receiver
void can_receive_task(void *arg)
{
    telemetry_t frame = {0}; // Temp storage for frame assembling  
    twai_message_t msg;

    ESP_LOGI(TAG, "CAN task started");

    while (1) {
        if (twai_receive(&msg, portMAX_DELAY) == ESP_OK) {
            payload_t val1, val2;
            
            // Unpack 8 bytes in two 4 byte vars
            for(int i=0; i<4; i++) {
                val1.bytes[i] = msg.data[i];
                val2.bytes[i] = msg.data[i+4];
            }

            // Put data depending on msg id
            if (msg.identifier == 0x10) {
                frame.timestamp = val1.u_val;
                frame.packet_id = val2.u_val;
            } 
            else if (msg.identifier == 0x11) {
                frame.chamber_pressure = val1.f_val;
                frame.tank_pressure = val2.f_val;
            } 
            else if (msg.identifier == 0x12) {
                frame.accel_z = val1.f_val;
                frame.altitude = val2.f_val;
                
                // ID 0x12 is the last one after that sending it to queue
                if (xQueueSend(telemetry_queue, &frame, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Queue full. Data went to Valhalla :( ");
                }
            }
        }
    }
}

//SD Card Writer
void sd_write_task(void *arg)
{
    ESP_LOGI(TAG, "SD Task Started. Opening file: %s", current_filename);
    
    FILE *f = fopen(current_filename, "a");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file in task");
        vTaskDelete(NULL); 
    }

    telemetry_t received_frame;
    int sync_counter = 0;

    while (1) {
        // Wait for data from the CAN task
        if (xQueueReceive(telemetry_queue, &received_frame, portMAX_DELAY) == pdTRUE) {
            
            // Simple write directly to the file stream
            fprintf(f, "%lu,%lu,%.2f,%.2f,%.2f,%.2f\n",
                    received_frame.timestamp,
                    received_frame.packet_id,
                    received_frame.chamber_pressure,
                    received_frame.tank_pressure,
                    received_frame.accel_z,
                    received_frame.altitude);
            
            sync_counter++;

            //Hardware sync every 100 packets (every 2 seconds at 50Hz)
            if (sync_counter >= 100) {
                fflush(f);
                fsync(fileno(f));
                sync_counter = 0;
            }
        }
    }
}


void app_main(void)
{
    esp_err_t ret;

    //Mount SD Card
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };
    sdmmc_card_t *card;
    
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SPI_MOSI_PIN,
        .miso_io_num = SPI_MISO_PIN,
        .sclk_io_num = SPI_CLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };
    spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);

    sdmmc_host_t host_config = SDSPI_HOST_DEFAULT();
    host_config.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SPI_CS_PIN;
    slot_config.host_id = host_config.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host_config, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card");
        return;
    }

    struct stat st;
    int file_index = 1;
    
    while (1) {
        sprintf(current_filename, "/sdcard/log_%03d.csv", file_index);

        if (stat(current_filename, &st) == 0) {
            file_index++; 
        } else {
            break; 
        }
    }

    ESP_LOGI(TAG, "Creating new flight log: %s", current_filename);
    
    // Write CSV Header
    FILE *f = fopen(current_filename, "w");
    if (f != NULL) {
        fprintf(f, "timestamp,packet_id,chamber_pressure,tank_pressure,accel_z,altitude\n");
        fclose(f);
    }

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();

    telemetry_queue = xQueueCreate(1000, sizeof(telemetry_t));

    xTaskCreate(can_receive_task, "can_task", 4096, NULL, 5, NULL);
    xTaskCreate(sd_write_task, "sd_task", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "System Ready. Waiting for CAN data");
}