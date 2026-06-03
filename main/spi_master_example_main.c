// #include <stdio.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "driver/gpio.h"
// #include "esp_log.h"

// static const char *TAG = "DEBUG";

// #define PIN_DC    15
// #define PIN_CS     2
// #define PIN_MOSI  23
// #define PIN_CLK   18
// #define PIN_BLK    5
// #define PIN_RST   22

// void app_main(void)
// {
//     /* Init all pins as output */
//     gpio_reset_pin(PIN_DC);
//     gpio_reset_pin(PIN_CS);
//     gpio_reset_pin(PIN_MOSI);
//     gpio_reset_pin(PIN_CLK);
//     gpio_reset_pin(PIN_BLK);
//     gpio_reset_pin(PIN_RST);

//     gpio_set_direction(PIN_DC,   GPIO_MODE_OUTPUT);
//     gpio_set_direction(PIN_CS,   GPIO_MODE_OUTPUT);
//     gpio_set_direction(PIN_MOSI, GPIO_MODE_OUTPUT);
//     gpio_set_direction(PIN_CLK,  GPIO_MODE_OUTPUT);
//     gpio_set_direction(PIN_BLK,  GPIO_MODE_OUTPUT);
//     gpio_set_direction(PIN_RST,  GPIO_MODE_OUTPUT);

//     ESP_LOGI(TAG, "Starting pin toggle test");
//     ESP_LOGI(TAG, "BLK=IO5 DC=IO15 CS=IO2 MOSI=IO23 CLK=IO18 RST=IO22");

//     /* Step 1 - all LOW */
//     ESP_LOGI(TAG, "Step1: ALL LOW");
//     gpio_set_level(PIN_DC,   0);
//     gpio_set_level(PIN_CS,   0);
//     gpio_set_level(PIN_MOSI, 0);
//     gpio_set_level(PIN_CLK,  0);
//     gpio_set_level(PIN_BLK,  0);
//     gpio_set_level(PIN_RST,  0);
//     vTaskDelay(pdMS_TO_TICKS(2000));

//     /* Step 2 - BLK only HIGH */
//     ESP_LOGI(TAG, "Step2: BLK HIGH only - screen should light up");
//     gpio_set_level(PIN_BLK, 1);
//     vTaskDelay(pdMS_TO_TICKS(3000));

//     /* Step 3 - BLK LOW */
//     ESP_LOGI(TAG, "Step3: BLK LOW - screen should go dark");
//     gpio_set_level(PIN_BLK, 0);
//     vTaskDelay(pdMS_TO_TICKS(2000));

//     /* Step 4 - RST cycle */
//     ESP_LOGI(TAG, "Step4: RST cycle");
//     gpio_set_level(PIN_RST, 0);
//     vTaskDelay(pdMS_TO_TICKS(500));
//     gpio_set_level(PIN_RST, 1);
//     vTaskDelay(pdMS_TO_TICKS(500));

//     /* Step 5 - all HIGH */
//     ESP_LOGI(TAG, "Step5: ALL HIGH");
//     gpio_set_level(PIN_DC,   1);
//     gpio_set_level(PIN_CS,   1);
//     gpio_set_level(PIN_MOSI, 1);
//     gpio_set_level(PIN_CLK,  1);
//     gpio_set_level(PIN_BLK,  1);
//     gpio_set_level(PIN_RST,  1);
//     vTaskDelay(pdMS_TO_TICKS(2000));

//     ESP_LOGI(TAG, "Toggle test done - report what happened");

//     /* Continuous BLK blink so you can confirm IO5 works */
//     ESP_LOGI(TAG, "Blinking BLK every 500ms forever");
//     while (1) {
//         gpio_set_level(PIN_BLK, 1);
//         ESP_LOGI(TAG, "BLK HIGH");
//         vTaskDelay(pdMS_TO_TICKS(500));
//         gpio_set_level(PIN_BLK, 0);
//         ESP_LOGI(TAG, "BLK LOW");
//         vTaskDelay(pdMS_TO_TICKS(500));
//     }
// }

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_system.h"

static const char *TAG = "ST7789";

/* ── PINS ── */
#define PIN_DC    15
#define PIN_CS     2
#define PIN_MOSI  23
#define PIN_CLK   18
#define PIN_BLK    5
#define PIN_RST   22

#define LCD_W     240
#define LCD_H     240

static spi_device_handle_t spi;

void lcd_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(PIN_DC, dc);
}

static void lcd_cmd(uint8_t cmd)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = 8;
    t.tx_buffer = &cmd;
    t.user      = (void *)0;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

static void lcd_data(const uint8_t *data, int len)
{
    if (len == 0) return;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = len * 8;
    t.tx_buffer = data;
    t.user      = (void *)1;
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

static void lcd_data_byte(uint8_t d)
{
    lcd_data(&d, 1);
}

static void lcd_set_window(uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1)
{
    uint8_t d[4];
    lcd_cmd(0x2A);
    d[0]=x0>>8; d[1]=x0&0xFF; d[2]=x1>>8; d[3]=x1&0xFF;
    lcd_data(d, 4);
    lcd_cmd(0x2B);
    d[0]=y0>>8; d[1]=y0&0xFF; d[2]=y1>>8; d[3]=y1&0xFF;
    lcd_data(d, 4);
    lcd_cmd(0x2C);
}

static void lcd_fill(uint16_t color)
{
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    lcd_set_window(0, 0, LCD_W-1, LCD_H-1);

    /* set DC high for data */
    gpio_set_level(PIN_DC, 1);

    uint8_t buf[2] = {hi, lo};
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length    = 16;
    t.tx_buffer = buf;
    t.user      = (void *)1;

    for (int i = 0; i < LCD_W * LCD_H; i++) {
        ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
    }
}

void app_main(void)
{
    printf("=== BOOT OK ===\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* ── GPIO ── */
    printf("Init GPIO...\n"); fflush(stdout);
    gpio_reset_pin(PIN_DC);
    gpio_reset_pin(PIN_RST);
    gpio_reset_pin(PIN_BLK);
    gpio_set_direction(PIN_DC,  GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_BLK, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_BLK, 0);
    printf("GPIO OK\n"); fflush(stdout);

    /* ── SPI BUS ── */
    printf("Init SPI bus...\n"); fflush(stdout);
    spi_bus_config_t buscfg = {
        .mosi_io_num     = PIN_MOSI,
        .sclk_io_num     = PIN_CLK,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_W * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    printf("SPI bus OK\n"); fflush(stdout);

    /* ── SPI DEVICE ── */
    printf("Add SPI device...\n"); fflush(stdout);
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000,   // 10MHz safe
        .mode           = 0,
        .spics_io_num   = PIN_CS,
        .queue_size     = 1,
        .pre_cb         = lcd_spi_pre_transfer_callback,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));
    printf("SPI device OK\n"); fflush(stdout);

    /* ── RESET ── */
    printf("Reset LCD...\n"); fflush(stdout);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    printf("Reset OK\n"); fflush(stdout);

    /* ── INIT SEQUENCE ── */
    printf("Sending init cmds...\n"); fflush(stdout);

    lcd_cmd(0x01);                      // SW reset
    vTaskDelay(pdMS_TO_TICKS(150));
    printf("SW reset done\n"); fflush(stdout);

    lcd_cmd(0x11);                      // Sleep out
    vTaskDelay(pdMS_TO_TICKS(120));
    printf("Sleep out done\n"); fflush(stdout);

    lcd_cmd(0x36); lcd_data_byte(0x00); // MADCTL
    lcd_cmd(0x3A); lcd_data_byte(0x55); // 16bit color
    printf("MADCTL+colorfmt done\n"); fflush(stdout);

    lcd_cmd(0xB2);                      // Porch
    lcd_data_byte(0x0C); lcd_data_byte(0x0C);
    lcd_data_byte(0x00); lcd_data_byte(0x33); lcd_data_byte(0x33);

    lcd_cmd(0xB7); lcd_data_byte(0x35); // Gate ctrl
    lcd_cmd(0xBB); lcd_data_byte(0x19); // VCOM
    lcd_cmd(0xC0); lcd_data_byte(0x2C); // LCM
    lcd_cmd(0xC2); lcd_data_byte(0x01); // VDV/VRH enable
    lcd_cmd(0xC3); lcd_data_byte(0x12); // VRH
    lcd_cmd(0xC4); lcd_data_byte(0x20); // VDV
    lcd_cmd(0xC6); lcd_data_byte(0x0F); // Frame rate
    lcd_cmd(0xD0); lcd_data_byte(0xA4); lcd_data_byte(0xA1); // Power

    printf("Sending gamma...\n"); fflush(stdout);
    lcd_cmd(0xE0);
    uint8_t gp[] = {0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,
                    0x54,0x4C,0x18,0x0D,0x0B,0x1F,0x23};
    lcd_data(gp, 14);
    lcd_cmd(0xE1);
    uint8_t gn[] = {0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,
                    0x44,0x51,0x2F,0x1F,0x1F,0x20,0x23};
    lcd_data(gn, 14);

    lcd_cmd(0x21);                      // Inversion ON
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_cmd(0x29);                      // Display ON
    vTaskDelay(pdMS_TO_TICKS(120));
    printf("Init sequence done\n"); fflush(stdout);

    /* ── BACKLIGHT ON ── */
    gpio_set_level(PIN_BLK, 1);
    printf("Backlight ON\n"); fflush(stdout);

    /* ── FILL RED ── */
    printf("Filling RED...\n"); fflush(stdout);
    lcd_fill(0xF800);
    printf("RED done\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("Filling GREEN...\n"); fflush(stdout);
    lcd_fill(0x07E0);
    printf("GREEN done\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("Filling BLUE...\n"); fflush(stdout);
    lcd_fill(0x001F);
    printf("BLUE done\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2000));

    printf("=== ALL DONE ===\n"); fflush(stdout);

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}