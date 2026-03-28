#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/twai.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "SIMULATOR";

#define TX_PIN 26
#define RX_PIN 25

typedef union {
    float f_val;
    uint32_t u_val;
    uint8_t bytes[4];
} payload_t;

//flight state list
typedef enum {
    STATE_IDLE,
    STATE_BOOST,
    STATE_COAST,
    STATE_DESCENT,
    STATE_LANDED
} flight_state_t;

void app_main(void)
{
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_PIN, RX_PIN, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    twai_driver_install(&g_config, &t_config, &f_config);
    twai_start();

    flight_state_t current_state = STATE_IDLE;
    uint32_t packet_id = 0;
    uint32_t burn_start_time = 0; // Engine ignition time
    
    float altitude_m = 0.0f;
    float velocity_ms = 0.0f;
    float accel_z = 9.81f; 
    float chamber_pressure = 1.0f; 
    float tank_pressure = 50.0f;

    ESP_LOGI(TAG, "Rocket Simulator Started");

    while (1) {
        uint32_t current_time_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);

        // --- UPDATED FLIGHT PHYSICS ---
        if (current_state == STATE_IDLE) {
            accel_z = 9.81f;
            altitude_m = 0.0f;
            velocity_ms = 0.0f;
            
            if (current_time_ms > 10000) {
                current_state = STATE_BOOST;
                burn_start_time = current_time_ms; // Remember launch time
                ESP_LOGI(TAG, "IGNITION! Switching to BOOST phase.");
            }
        } 
        else if (current_state == STATE_BOOST) {
            chamber_pressure = 45.5f;
            
            // Limit tank pressure to not make it negative
            if (tank_pressure > 1.0f) tank_pressure -= 0.8f; 
            accel_z = 65.0f; // Acceleration at launch
            
            velocity_ms += (accel_z - 9.81f) * 0.02f; 
            altitude_m += velocity_ms * 0.02f;

            // Engine burns for 3 secs 
            if (current_time_ms - burn_start_time > 3000) {
                current_state = STATE_COAST;
                chamber_pressure = 1.0f; // Engine off
                ESP_LOGI(TAG, "MECO (Engine Off). Coasting");
            }
        }
        else if (current_state == STATE_COAST) {
            accel_z = 0.0f; // Coasting (zero-g)
            
            //  gravity slows down the rocket
            velocity_ms += (0.0f - 9.81f) * 0.02f; 
            altitude_m += velocity_ms * 0.02f;

            // If velocity drops below zero rocket descending
            if (velocity_ms <= 0.0f) {
                current_state = STATE_DESCENT;
                ESP_LOGI(TAG, "Apogee reached! Descending");
            }
        }
        else if (current_state == STATE_DESCENT) {
            accel_z = 0.0f; 
            
            //Continue falling (I know there should be a parachute but let's keep it simple :))
            velocity_ms += (0.0f - 9.81f) * 0.02f; 
            altitude_m += velocity_ms * 0.02f;

            if (altitude_m <= 0.0f) {
                altitude_m = 0.0f;
                velocity_ms = 0.0f;
                current_state = STATE_LANDED;
                ESP_LOGI(TAG, "TOUCHDOWN! Rocket has landed (Or whatever remains of it...it doesn't had a parachute :D)");
            }
        }
        else if (current_state == STATE_LANDED) {
            accel_z = 9.81f; // Back on the ground
            chamber_pressure = 1.0f;
        }


        // CAN message package 
        twai_message_t msg;
        msg.extd = 0;
        msg.rtr = 0;
        msg.data_length_code = 8; 
        payload_t val1, val2;

        msg.identifier = 0x10;
        val1.u_val = current_time_ms;
        val2.u_val = packet_id;
        for(int i=0; i<4; i++) { msg.data[i] = val1.bytes[i]; msg.data[i+4] = val2.bytes[i]; }
        twai_transmit(&msg, pdMS_TO_TICKS(10));

        msg.identifier = 0x11;
        val1.f_val = chamber_pressure;
        val2.f_val = tank_pressure;
        for(int i=0; i<4; i++) { msg.data[i] = val1.bytes[i]; msg.data[i+4] = val2.bytes[i]; }
        twai_transmit(&msg, pdMS_TO_TICKS(10));

        msg.identifier = 0x12;
        val1.f_val = accel_z;
        val2.f_val = altitude_m;
        for(int i=0; i<4; i++) { msg.data[i] = val1.bytes[i]; msg.data[i+4] = val2.bytes[i]; }
        twai_transmit(&msg, pdMS_TO_TICKS(10));

        //Frequency 1 Hz on ground 50 Hz in flight
        int delay_ms;
        if (current_state == STATE_IDLE || current_state == STATE_LANDED) {
            delay_ms = 1000; 
        } else {
            delay_ms = 20;   
        }

        packet_id++;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}