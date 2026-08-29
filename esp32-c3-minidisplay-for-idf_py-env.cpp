/*
idf_component_register(SRCS "esp32-c3-minidisplay-for-idf_py-env.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES driver nixy4__u8g2
                    PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi driver
)
*/

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "u8g2.h"
#include "driver/i2c.h"

#define I2C_MASTER_SCL_IO 6
#define I2C_MASTER_SDA_IO 5
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

static const char *TAG = "OLED_DEBUG";

bool displayInitialized = false;
u8g2_t u8g2;

// 요청하신 하드웨어 물리 특성값 및 오프셋 제약조건 고정 적용
static constexpr int viewWidth = 72;         
static constexpr int viewHeight = 40;        
static constexpr int xOffset = (128 - 72) / 2;     
static constexpr int yOffset = (64 - 40) / 2;      

extern "C" uint8_t u8x8_byte_esp32_hw_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    static uint8_t buffer[128];
    static uint8_t buf_idx = 0;
    static uint8_t dc_val = 0;
    uint8_t *data;

    switch (msg) {
        case U8X8_MSG_BYTE_INIT:
            break;
        case U8X8_MSG_BYTE_SET_DC:
            dc_val = arg_int; // 0: Command, 1: Data
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)arg_ptr;
            for (uint8_t i = 0; i < arg_int; i++) {
                if (buf_idx < sizeof(buffer)) {
                    buffer[buf_idx++] = data[i];
                }
            }
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            {
                i2c_cmd_handle_t cmd = i2c_cmd_link_create();
                i2c_master_start(cmd);
                i2c_master_write_byte(cmd, (0x3C << 1) | I2C_MASTER_WRITE, true);
                
                uint8_t control_byte = (dc_val == 0) ? 0x00 : 0x40;
                i2c_master_write_byte(cmd, control_byte, true);
                
                if (buf_idx > 0) {
                    i2c_master_write(cmd, buffer, buf_idx, true);
                }
                
                i2c_master_stop(cmd);
                esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
                i2c_cmd_link_delete(cmd);
                
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "[I2C_ERROR] Transmission failed! Err: %d", ret);
                    return 0;
                }
            }
            break;
        default:
            return 0;
    }
    return 1;
}

extern "C" uint8_t u8x8_gpio_and_delay_esp32(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch (msg) {
        case U8X8_MSG_DELAY_MILLI:
            vTaskDelay(pdMS_TO_TICKS(arg_int));
            break;
        case U8X8_MSG_DELAY_10MICRO:
            vTaskDelay(pdMS_TO_TICKS(1));
            break;
        default:
            return 1;
    }
    return 1;
}

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    conf.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_TX_BUF_DISABLE, I2C_MASTER_RX_BUF_DISABLE, 0);
}

void send_ssd1306_72x40_cmds() {
    uint8_t cmds[] = {
        0xA8, 0x27, // Set Multiplex Ratio -> 40 (0x27)
        0xD3, 0x00, // Set Display Offset -> 0
        0x40,       // Set Display Start Line -> 0
        0xDA, 0x12  // Set COM Pins Hardware Configuration
    };

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (0x3C << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x00, true);
    i2c_master_write(cmd, cmds, sizeof(cmds), true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
}

void initDisplay() {
    if (i2c_master_init() != ESP_OK) {
        displayInitialized = false;
        return;
    }

    u8g2_Setup_ssd1306_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_esp32_hw_i2c, u8x8_gpio_and_delay_esp32);
    u8g2_InitDisplay(&u8g2);
    send_ssd1306_72x40_cmds();
    u8g2_SetPowerSave(&u8g2, 0);
    u8g2_SetContrast(&u8g2, 255);
    
    displayInitialized = true;
}

void drawOledScreen(int uptime) {
    u8g2_ClearBuffer(&u8g2);
    
    // 상단 텍스트 (HELLO)
    u8g2_SetFont(&u8g2, u8g2_font_9x15_tf);
    u8g2_DrawStr(&u8g2, xOffset, yOffset, "HELLO00");
    
    // 하단 텍스트 (1레벨 더 큰 폰트 적용: u8g2_font_4x6_tr -> u8g2_font_5x8_tr)
    u8g2_SetFont(&u8g2, u8g2_font_5x8_tr);
    char uptimeBuffer[20];
    snprintf(uptimeBuffer, sizeof(uptimeBuffer), "Uptime: %ds", uptime);
    u8g2_DrawStr(&u8g2, xOffset, yOffset + 15, uptimeBuffer);
    
    u8g2_SendBuffer(&u8g2);
}

extern "C" void app_main(void) {
    initDisplay();

    int uptimeCounter = 0;
    while (1) {
        if (displayInitialized) {
            drawOledScreen(uptimeCounter);
            ESP_LOGI(TAG, "Uptime: %d seconds", uptimeCounter);
            uptimeCounter++;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
